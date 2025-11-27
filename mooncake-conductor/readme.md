## compile

按照在mooncake项目顶层目录下的流程进行编译:
```bash
bash [mooncake目录下的dependencies.sh]
bash [mooncake-conductor目录下的dependencies.sh]

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

## FAQ
当前的序列化会使用`boost/python.hpp`，而它依赖于环境中的Python动态库，如果在运行`mooncake_conductor`程序时出现：
```bash
mooncake_conductor: error while loading shared libraries: libpython3.xx.so.x.x: cannot open shared object file: No such file or directory
```
，则按照以下步骤即可解决：
 1. 找到对应的动态库文件的路径：
    ```bash
    find / -name "libpython3.xx.so.x.x"
    ```

 2. 临时解决方案 (**只在当前终端窗口生效**)：
    ```bash
    export LD_LIBRARY_PATH=[动态库文件所在目录]:$LD_LIBRARY_PATH
    ```
    长期解决方案：
    * 写入配置文件：
        ```bash
        echo "[动态库文件所在目录]" | tee -a /etc/ld.so.conf.d/python3.conf
        ```
    * **或**通过软链接到
        ```bash
        ln -s [动态库文件路径] /usr/lib/
        ```
    * 更新到系统库缓存
        ```bash
        ldconfig
        ```
 3. 重新执行`mooncake_conductor`命令即可
