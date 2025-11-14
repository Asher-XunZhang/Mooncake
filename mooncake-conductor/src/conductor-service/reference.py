from typing import Any, NewType
from collections.abc import Callable, Sequence

import hashlib
import pickle

BlockHash = NewType("BlockHash", bytes)
NONE_HASH: BlockHash


def sha256(input: Any) -> bytes:
    input_bytes = pickle.dumps(input, protocol=pickle.HIGHEST_PROTOCOL)
    return hashlib.sha256(input_bytes).digest()


def get_hash_fn_by_name(hash_fn_name: str) -> Callable[[Any], bytes]:
    """Get a hash function by name, or raise an error if the function is not found.

    Args:
        hash_fn_name: Name of the hash function.

    Returns:
        A hash function.
    """
    if hash_fn_name == "sha256":
        return sha256



def hash_block_tokens(
    parent_block_hash: BlockHash | None,
    curr_block_token_ids: Sequence[int],
    extra_keys: tuple[Any, ...] | None = None,
) -> BlockHash:
    """Computes a hash value corresponding to the contents of a block and
    the contents of the preceding block(s). The hash value is used for
    prefix caching. We use LRU cache for this function to avoid recomputing
    hash values for the same block contents.
    Args:
        parent_block_hash: The hash of the parent block. None
            if this is the first block.
        curr_block_token_ids: A list of token ids in the current
            block. The current block is assumed to be full.
        extra_keys: Extra keys for the block.
    Returns:
        The hash value of the block and the token ids in the block.
        The entire tuple is used as the hash key of the block.
    """
    if not parent_block_hash:
        parent_block_hash = NONE_HASH

    curr_block_token_ids_tuple = tuple(curr_block_token_ids)
    return BlockHash(
        sha256((parent_block_hash, curr_block_token_ids_tuple, extra_keys))
    )

def get_request_block_hasher(
    request,
    block_size
):
    """
    Returns a function which computes the list of un-computed block hashes
    of a request."""
    # 直接初始化为0就行, 对于conductor，请求的调度是一次性的
    # start_token_idx = len(request.block_hashes) * block_size
    start_token_idx = 0
    
    num_tokens = request.num_tokens # 输入的token个数

    if start_token_idx + block_size > num_tokens:
        # Early stop when there no new full blocks created.
        return []

    # prev_block_hash_value = (
    #     request.block_hashes[-1] if request.block_hashes else None   # 直接设置为None就行
    # )
    prev_block_hash_value = None
    new_block_hashes: list[BlockHash] = []
    while True:
        end_token_idx = start_token_idx + block_size
        if end_token_idx > num_tokens:
            # We only hash full blocks
            break

        # Compute the hash of the current block
        block_tokens = request.all_token_ids[start_token_idx:end_token_idx]
        block_hash = hash_block_tokens(
            prev_block_hash_value, block_tokens
        )

        new_block_hashes.append(block_hash)
        start_token_idx += block_size
        prev_block_hash_value = block_hash

    return new_block_hashes

class Request:
    def __init__(self, num_tokens: int, block_hashes=None, all_token_ids=None):
        self.num_tokens = num_tokens
        self.block_hashes = block_hashes
        self.all_token_ids = all_token_ids


if __name__ == "__main__":
    # 假设block_size为5, token_id_list 为 [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    token_id_list = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
    request = Request(num_tokens=len(token_id_list), all_token_ids=token_id_list)
    block_hashes = get_request_block_hasher(request, block_size=5)
    print(block_hashes)