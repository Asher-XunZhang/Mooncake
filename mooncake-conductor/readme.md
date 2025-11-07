## compile
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

mooncake_conductor --port=8080 --prefiller_hosts="127.0.0.1,127.0.0.1" --prefiller_ports="8001,8002"

## 停止server
可以输入ctrl +c 停止server