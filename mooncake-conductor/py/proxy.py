# Adapted from https://github.com/vllm-project/vllm/tests/v1/kv_connector/nixl_integration/toy_proxy_server.py

import argparse
import asyncio
import functools
import heapq
import json
import os
import sys
import socket
import uuid
from contextlib import asynccontextmanager
from dataclasses import dataclass
from typing import Any, List

import httpx
from fastapi import FastAPI, Request
from fastapi.responses import StreamingResponse
# from vllm.logger import init_logger
from logging import getLogger

logger = getLogger(__name__)

# Add uvloop for faster event loop if available
try:
    import uvloop

    asyncio.set_event_loop_policy(uvloop.EventLoopPolicy())
except ImportError:
    pass

try:
    from mooncake.store import MooncakeDistributedStore
except ImportError:
    logger.error("Failed to import MooncakeDistributedStore. Please check if the Mooncake is installed.")
    sys.exit(1)


def find_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('localhost', 0))
        addr, port = s.getsockname()
    return port


@dataclass
class MooncakeStoreConfig:
    local_hostname: str
    metadata_server: str
    global_segment_size: int
    local_buffer_size: int
    protocol: str
    device_name: str
    master_server_address: str
    use_ascend_direct: bool

    @staticmethod
    def from_file(file_path: str) -> "MooncakeStoreConfig":
        with open(file_path) as file:
            config = json.load(file)
        return MooncakeStoreConfig(
            local_hostname=config.get("local_hostname"),
            metadata_server=config.get("metadata_server"),
            global_segment_size=config.get("global_segment_size", 3355443200),
            local_buffer_size=config.get("local_buffer_size", 1073741824),
            protocol=config.get("protocol", "tcp"),
            device_name=config.get("device_name", ""),
            master_server_address=config.get("master_server_address"),
            use_ascend_direct=config.get("use_ascend_direct", False))

    @staticmethod
    def load_from_env() -> "MooncakeStoreConfig":
        config_path = os.getenv("MOONCAKE_CONFIG_PATH")
        if not config_path:
            raise ValueError(
                "The environment variable 'MOONCAKE_CONFIG_PATH' is not set.")
        return MooncakeStoreConfig.from_file(config_path)


class BestPrefillResult:
    def __init__(self):
        self.hit = False
        self.best_index = 0
        self.best_key = ""
        self.node_id = ""



mooncake_client = MooncakeDistributedStore()
config = MooncakeStoreConfig.load_from_env()
mooncake_client.setup(config.local_hostname+find_free_port(), config.metadata_server,
                                   config.global_segment_size,
                                   config.local_buffer_size,
                                   config.protocol,
                                   config.device_name,
                                   config.master_server_address)

def extract_node_ids(resp):
    node_ids = []
    for rep_desc in resp.replicas:
        if not hasattr(rep_desc, 'status') or rep_desc.status != "COMPLETE":
            continue

        if hasattr(rep_desc, 'is_memory_replica') and rep_desc.is_memory_replica():
            mem_desc = rep_desc.get_memory_descriptor()
            if hasattr(mem_desc, 'buffer_descriptors') and mem_desc.buffer_descriptors:
                first_buffer = mem_desc.buffer_descriptors[0]
                if hasattr(first_buffer, 'transport_endpoint_'):
                    node_ids.append(first_buffer.transport_endpoint_)
    return node_ids

class ServerState:

    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.url = f'http://{host}:{port}/v1'
        self.client = httpx.AsyncClient(timeout=None,
                                        base_url=self.url,
                                        limits=httpx.Limits(
                                            max_connections=100000,
                                            max_keepalive_connections=100000))
        self.active_tokens = 0
        self.active_kv_cache = 0  # Only for prefiller
        self.active_requests = 0  # Number of active requests
        self.aborted_requests = set()  # Track aborted requests


class ProxyState:

    def __init__(self, prefiller_instances, decoder_instances):
        self.prefillers: List[ServerState] = [
            ServerState(h, p) for h, p in prefiller_instances
        ]
        self.decoders: List[ServerState] = [
            ServerState(h, p) for h, p in decoder_instances
        ]
        self.req_to_prefiller = {}
        self.req_id_lock = asyncio.Lock()
        # Removed selection locks - no longer needed for synchronous methods

        # Initialize priority queues for efficient server selection
        # Each entry is (priority_score, server_index, server_reference)
        # Lower priority score = higher priority (less loaded)
        self.prefiller_heap = [(0, i, server)
                               for i, server in enumerate(self.prefillers)]
        self.decoder_heap = [(0, i, server)
                             for i, server in enumerate(self.decoders)]
        heapq.heapify(self.prefiller_heap)
        heapq.heapify(self.decoder_heap)

    def _update_prefiller_priority(self, server_idx: int):
        """Update the priority of a prefiller server in the heap."""
        server = self.prefillers[server_idx]
        # Priority based on active_tokens and active_kv_cache
        priority = server.active_tokens + server.active_kv_cache * 0.3
        # Remove old entry and add new one
        self.prefiller_heap = [(p, i, s) for p, i, s in self.prefiller_heap
                               if i != server_idx]
        heapq.heappush(self.prefiller_heap,
                       (priority, server_idx, server))  # type: ignore

    def _update_decoder_priority(self, server_idx: int):
        """Update the priority of a decoder server in the heap."""
        server = self.decoders[server_idx]
        priority = server.active_tokens
        # Remove old entry and add new one
        self.decoder_heap = [(p, i, s) for p, i, s in self.decoder_heap
                             if i != server_idx]
        heapq.heappush(self.decoder_heap,
                       (priority, server_idx, server))  # type: ignore

    def abort_prefiller_request(self, server_idx: int,
                                request_id):  # Changed to synchronous
        """
        Mark a request as aborted. This will helps to release kv cache in
        prefiller node.
        """
        # No lock needed - atomic operation
        self.prefillers[server_idx].aborted_requests.add(request_id)

    def aquire_aborted_prefiller_requests(
            self, server_idx: int):
        """
        Get the set of aborted requests and clear it.
        This is used to release kv cache in prefiller node.
        """
        # No lock needed - atomic operation
        aborted_requests = self.prefillers[server_idx].aborted_requests.copy()
        self.prefillers[server_idx].aborted_requests.clear()
        return aborted_requests

    async def next_req_id(self):
        async with self.req_id_lock:
            return str(uuid.uuid4())

    def select_prefiller(self, token_count):  # Changed to synchronous
        # No lock needed - entire function is atomic
        if not self.prefiller_heap:
            raise RuntimeError("No prefiller servers available")

        priority, chosen, server = heapq.heappop(self.prefiller_heap)

        # Update the chosen server atomically
        self.prefillers[chosen].active_tokens += token_count
        self.prefillers[chosen].active_kv_cache += token_count

        # Update priority and re-add to heap
        self._update_prefiller_priority(chosen)

        return chosen

    def release_prefiller(self, idx, token_count):
        # No lock needed - atomic operation
        self.prefillers[idx].active_tokens -= token_count
        # Update priority queue after releasing
        self._update_prefiller_priority(idx)

    def release_prefiller_kv(self, idx, token_count):
        # No lock needed - atomic operation
        if self.prefillers[idx].active_kv_cache > 0:
            self.prefillers[idx].active_kv_cache -= token_count
        # Update priority queue after releasing
        self._update_prefiller_priority(idx)

    def select_decoder(self, token_count):  # Changed to synchronous
        # No lock needed - entire function is atomic
        if not self.decoder_heap:
            raise RuntimeError("No decoder servers available")

        priority, chosen, server = heapq.heappop(self.decoder_heap)

        # Update the chosen server atomically
        self.decoders[chosen].active_tokens += token_count

        # Update priority and re-add to heap
        self._update_decoder_priority(chosen)

        return chosen

    def release_decoder(self, idx, token_count):  # Changed to synchronous
        # No lock needed - atomic operation
        self.decoders[idx].active_tokens -= token_count
        # Update priority queue after releasing
        self._update_decoder_priority(idx)

    # Omni_infer's calculate_input_scores function
    def calculate_prefill_scores(self, request_length: int) -> float:
        length_score = request_length / 4.0
        input_score = length_score * 0.0345 + 120.0745
        return input_score

    def calculate_decode_scores(self, request_length: int) -> float:
        return request_length

    def find_best_prefill(self, keys, results):
        out = BestPrefillResult()
        
        if len(keys) != len(results):
            print(f"PrefillPlanner::find_best_prefill: keys/results size mismatch: {len(keys)} vs {len(results)}")
            return out
        
        if not keys:
            return out
        
        N = len(keys)
        
        node_prefix_hit = {}
        
        for i in range(N):
            res = results[i]
            
            if hasattr(res, 'has_value') and not res.has_value():
                continue
            
            if hasattr(res, 'value'):
                resp = res.value()
            else:
                resp = res
            
            if not hasattr(resp, 'replicas') or not resp.replicas:
                continue
            
            node_ids = self.extract_node_ids(resp)
            for node_id in node_ids:
                if node_id not in node_prefix_hit:
                    node_prefix_hit[node_id] = [False] * N
                if i < len(node_prefix_hit[node_id]):
                    node_prefix_hit[node_id][i] = True
        
        best_length = 0
        best_node = ""
        
        for node_id, hits in node_prefix_hit.items():
            length = 0
            while length < N and length < len(hits) and hits[length]:
                length += 1
            
            if length > best_length:
                best_length = length
                best_node = node_id
        
        if best_length == 0 or not best_node:
            return out
        
        best_index = best_length - 1
        
        out.hit = True
        out.best_index = best_index
        out.best_key = keys[best_index]
        out.node_id = best_node
        return out

    def get_best_prefill(self, request):
        # TODO get key
        keys = {"123123"}
        # get replica
        replica = mooncake_client.batch_get_replica(keys)
        result = self.find_best_prefill(keys, replica)
        # TODO get prefiller idx
        prefill_indx = result
        return prefill_indx


proxy_state = None


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--host", type=str, default="localhost")
    parser.add_argument("--prefiller-hosts",
                        type=str,
                        nargs="+",
                        default=["localhost"])
    parser.add_argument("--prefiller-ports",
                        type=int,
                        nargs="+",
                        default=[8001])
    parser.add_argument("--decoder-hosts",
                        type=str,
                        nargs="+",
                        default=["localhost"])
    parser.add_argument("--decoder-ports", type=int, nargs="+", default=[8002])
    parser.add_argument("--max-retries",
                        type=int,
                        default=3,
                        help="Maximum number of retries for HTTP requests")
    parser.add_argument(
        "--retry-delay",
        type=float,
        default=0.001,
        help="Base delay (seconds) for exponential backoff retries")
    args = parser.parse_args()
    if len(args.prefiller_hosts) != len(args.prefiller_ports):
        raise ValueError(
            "Number of prefiller hosts must match number of prefiller ports")
    if len(args.decoder_hosts) != len(args.decoder_ports):
        raise ValueError(
            "Number of decoder hosts must match number of decoder ports")
    args.prefiller_instances = list(
        zip(args.prefiller_hosts, args.prefiller_ports))
    args.decoder_instances = list(zip(args.decoder_hosts, args.decoder_ports))
    return args


@asynccontextmanager
async def lifespan(app: FastAPI):
    global proxy_state
    proxy_state = ProxyState(global_args.prefiller_instances,
                             global_args.decoder_instances)
    print(
        f"Initialized {len(proxy_state.prefillers)} prefill clients and {len(proxy_state.decoders)} decode clients."
    )
    yield
    for p in proxy_state.prefillers:
        await p.client.aclose()
    for d in proxy_state.decoders:
        await d.client.aclose()


async def listen_for_disconnect(request: Request) -> None:
    """Return if a disconnect message is received"""
    while True:
        message = await request.receive()
        if message["type"] == "http.disconnect":
            break


def with_cancellation(handler_func):

    @functools.wraps(handler_func)
    async def wrapper(*args, **kwargs):
        request = kwargs["request"]
        handler_task = asyncio.create_task(handler_func(*args, **kwargs))
        cancellation_task = asyncio.create_task(listen_for_disconnect(request))
        done, pending = await asyncio.wait([handler_task, cancellation_task],
                                           return_when=asyncio.FIRST_COMPLETED)
        for task in pending:
            task.cancel()
        if handler_task in done:
            return handler_task.result()
        return None

    return wrapper


app = FastAPI(lifespan=lifespan)


async def send_request_to_service(client: httpx.AsyncClient,
                                  prefiller_id: int,
                                  endpoint: str,
                                  req_data: dict,
                                  request_id: str,
                                  max_retries: int = 3,
                                  base_delay: float = 0.2):
    aborted_requests = proxy_state.aquire_aborted_prefiller_requests(
        prefiller_id)
    req_data = req_data.copy()
    req_data['kv_transfer_params'] = {
        "do_remote_decode": True,
        "do_remote_prefill": False,
        "remote_engine_id": None,
        "remote_block_ids": None,
        "remote_host": None,
        "remote_port": None,
        "aborted_request": list(aborted_requests),
    }
    req_data["stream"] = False
    req_data["max_tokens"] = 1
    req_data["min_tokens"] = 1
    if "stream_options" in req_data:
        del req_data["stream_options"]
    headers = {
        "Authorization": f"Bearer {os.environ.get('OPENAI_API_KEY')}",
        "X-Request-Id": request_id
    }
    last_exc = None
    for attempt in range(1, max_retries + 1):
        try:
            response = await client.post(endpoint,
                                         json=req_data,
                                         headers=headers)
            response.raise_for_status()
            return response
        except (httpx.RequestError, httpx.HTTPStatusError) as e:
            logger.warning(
                f"Attempt {attempt} failed for {endpoint}: {str(e)}")
            last_exc = e
            if attempt < max_retries:
                await asyncio.sleep(base_delay * (2**(attempt - 1)))
            else:
                logger.error(
                    f"All {max_retries} attempts failed for {endpoint}.")
                raise last_exc


async def stream_service_response_with_retry(client: httpx.AsyncClient,
                                             endpoint: str,
                                             req_data: dict,
                                             request_id: str,
                                             max_retries: int = 3,
                                             base_delay: float = 0.2):
    headers = {
        "Authorization": f"Bearer {os.environ.get('OPENAI_API_KEY')}",
        "X-Request-Id": request_id
    }
    for attempt in range(1, max_retries + 1):
        try:
            async with client.stream("POST",
                                     endpoint,
                                     json=req_data,
                                     headers=headers) as response:
                response.raise_for_status()
                first_chunk_sent = False
                async for chunk in response.aiter_bytes():
                    first_chunk_sent = True
                    yield chunk
                return  # Success, exit after streaming
        except (httpx.RequestError, httpx.HTTPStatusError) as e:
            if attempt < max_retries:
                logger.warning(
                    f"Attempt {attempt} failed for streaming {endpoint}: {str(e)}"
                )
                await asyncio.sleep(base_delay * (2**(attempt - 1)))
            else:
                logger.error(
                    f"All {max_retries} attempts failed for streaming {endpoint}."
                )
                raise e
        except Exception as e:
            # If any chunk has been sent, do not retry, just log and drop
            if 'first_chunk_sent' in locals() and first_chunk_sent:
                logger.error(
                    f"Streaming to client interrupted after response started: {str(e)}"
                )
                return
            else:
                if attempt < max_retries:
                    logger.warning(
                        f"Attempt {attempt} failed for streaming {endpoint}: {str(e)}"
                    )
                    await asyncio.sleep(base_delay * (2**(attempt - 1)))
                else:
                    logger.error(
                        f"All {max_retries} attempts failed for streaming {endpoint}."
                    )
                    raise e


async def _handle_select_instance(api: str, req_data: Any,
                                  request_length: int):
    # prefiller_score = proxy_state.calculate_prefill_scores(request_length)
    # logger.debug(
    #     f"Request length: {request_length}, Prefiller score: {prefiller_score}"
    # )
    request_id = await proxy_state.next_req_id()

    # Select prefiller
    # prefiller_idx = proxy_state.select_prefiller(prefiller_score)
    prefiller_idx = proxy_state.get_best_prefill(req_data)
    prefiller = proxy_state.prefillers[prefiller_idx]

    # Send request to prefiller
    response = await send_request_to_service(
        prefiller.client,
        prefiller_idx,
        api,
        req_data,
        request_id,
        max_retries=global_args.max_retries,
        base_delay=global_args.retry_delay)
    # proxy_state.release_prefiller(prefiller_idx, prefiller_score)
    response_json = response.json()
    kv_transfer_params = response_json.get('kv_transfer_params', {})
    if kv_transfer_params:
        req_data["kv_transfer_params"] = kv_transfer_params
    # Select decoder
    decoder_score = proxy_state.calculate_decode_scores(request_length)
    logger.debug("Decoder score: %f", decoder_score)
    # Use the prefiller's kv_transfer_params to select decoder
    decoder_idx = proxy_state.select_decoder(decoder_score)
    decoder = proxy_state.decoders[decoder_idx]
    logger.debug("Using %s %s", prefiller.url, decoder.url)
    return InstanceInfo(request_id=request_id,
                        prefiller_idx=prefiller_idx,
                        prefiller_score=0.0,
                        prefiller=prefiller,
                        decoder=decoder,
                        decoder_idx=decoder_idx,
                        decoder_score=decoder_score)


@dataclass
class InstanceInfo:
    request_id: str
    prefiller_idx: int
    prefiller_score: float
    prefiller: ServerState
    decoder_idx: int
    decoder_score: float
    decoder: ServerState


async def _handle_completions(api: str, request: Request):
    try:
        req_data = await request.json()
        req_body = await request.body()
        request_length = len(req_body)
        instance_info = await _handle_select_instance(api, req_data,
                                                      request_length)
        stream_flag = bool(req_data.get("stream", False))
        chat_flag = "messages" in req_data

        if "prompt" in req_data:
            origin_prompt = req_data["prompt"]
        elif chat_flag:
            messages = req_data["messages"]
            origin_prompt = messages[0].get("content", "")
        else:
            origin_prompt = ""
        # refer to vLLM sampling_params: max_token default value
        origin_max_tokens = req_data.get("max_tokens", 16)

        async def generate_stream():
            nonlocal instance_info
            generated_token = ""
            released_kv = False
            retry_count = 0
            retry = True
            completion_tokens = 0
            # Only one await per chunk, minimal logic in loop
            try:
                while retry:
                    retry = False
                    async for chunk in stream_service_response_with_retry(
                            instance_info.decoder.client,
                            api,
                            req_data,
                            request_id=instance_info.request_id,
                            max_retries=global_args.max_retries,
                            base_delay=global_args.retry_delay):
                        if not released_kv and chunk:
                            # proxy_state.release_prefiller_kv(
                            #     instance_info.prefiller_idx,
                            #     instance_info.prefiller_score)
                            released_kv = True
                        chunk_str = chunk.decode("utf-8").strip()
                        if not chunk_str:
                            continue
                        if chunk_str.startswith("data: "):
                            chunk_str = chunk_str[len("data: "):]
                        try:
                            chunk_json = json.loads(chunk_str)
                        except json.JSONDecodeError:
                            # if chunk is [done], skip it.
                            logger.debug(
                                f"Skipping chunk: {chunk_str}")
                            yield chunk
                            continue
                        choices = chunk_json.get("choices", [])
                        if not choices:
                            yield chunk
                            continue

                        choice = choices[0]
                        delta = choice.get("delta") or {}
                        message = choice.get("message") or {}
                        content = (
                                delta.get("content")
                                or message.get("content")
                                or choice.get("text")
                                or ""
                                )
                        generated_token += content

                        stop_reason = choice.get(
                            "stop_reason")
                        usage = chunk_json.get("usage", {})
                        completion_tokens = (completion_tokens + 1) if stream_flag else \
                            (completion_tokens + usage.get("completion_tokens"))
                        if stop_reason == "recomputed":
                            retry = True
                            retry_count += 1
                            if chat_flag:
                                messages[0][
                                    "content"] = origin_prompt + generated_token
                            else:
                                req_data[
                                    "prompt"] = origin_prompt + generated_token
                            req_data[
                                "max_tokens"] = origin_max_tokens - completion_tokens + retry_count
                            tmp_request_length = len(
                                json.dumps(req_data).encode("utf-8"))
                            instance_info = await _handle_select_instance(
                                api, req_data, tmp_request_length)
                            break
                        if retry_count > 0 and not stream_flag:
                            if chat_flag:
                                choice["message"][
                                    "content"] = generated_token
                            else:
                                choice["text"] = generated_token
                            chunk = json.dumps(chunk_json).encode("utf-8")
                        yield chunk
            except Exception as e:
                logger.error(
                    f"Error during streaming from decoder {instance_info.decoder.url}: {str(e)} the aborted request {instance_info.request_id} will be routing to the target prefiller when new request is ready to dispatch to it"
                )
                proxy_state.abort_prefiller_request(
                    instance_info.prefiller_idx, instance_info.request_id)
                # proxy_state.release_prefiller_kv(instance_info.prefiller_idx,
                #                                  instance_info.prefiller_score)

            # After streaming done, release tokens
            proxy_state.release_decoder(instance_info.decoder_idx,
                                        instance_info.decoder_score)

        return StreamingResponse(generate_stream(),
                                 media_type="application/json")
    except Exception as e:
        import traceback
        exc_info = sys.exc_info()
        print("Error occurred in disagg prefill proxy server"
              f" - {api} endpoint")
        print(e)
        print("".join(traceback.format_exception(*exc_info)))
        raise


@app.post("/v1/completions")
@with_cancellation
async def handle_completions(request: Request):
    return await _handle_completions("/completions", request)


@app.post("/v1/chat/completions")
@with_cancellation
async def handle_chat_completions(request: Request):
    return await _handle_completions("/chat/completions", request)


@app.get("/healthcheck")
async def healthcheck():
    return {
        "status": "ok",
        "prefill_instances": len(proxy_state.prefillers),
        "decode_instances": len(proxy_state.decoders)
    }


if __name__ == '__main__':
    global global_args
    global_args = parse_args()
    import uvicorn

    uvicorn.run(app, host=global_args.host, port=global_args.port)
