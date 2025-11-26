## compile

> 依赖于`boost 1.82.0`以上的版本

按照mooncake流程进行编译
```bash
mkdir build
cd build
cmake ..
make -j
make install
```

完成后会自动将mooncake_conductor安装到/usr/local/bin

## 执行示例

```bash
mooncake_conductor --port=8080 --prefiller_hosts="127.0.0.1,127.0.0.1" --prefiller_ports="8001,8002"

mooncake_conductor --port=8180 --both_hosts="127.0.0.1" --both_ports="8100" --mooncake_store_port=50098 --mooncake_store_host="10.175.119.75"
```

## 停止server
可以输入`Ctrl + C`停止server