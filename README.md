# 简易信息提醒器

## 功能

- 通过web对提醒内容和时间进行设置
- 当红外感应读取到数据时，通过摄像头获取一帧图片，使用Tengine对图片识别分类，当识别到人且在为web设置的消息提醒时间段内时，在屏幕上显示消息10s
- 由于算力限制，每次识别一张图片需要约10s，所以无法做到实时检测，只能采用红外感应辅助检测。

## 哪吒d1-h

![IMG20241017193542](/home/ldh/qq_data/IMG20241017193542.jpg)



## UI

### web设置界面

![image-20241017194132100](/home/ldh/.config/Typora/typora-user-images/image-20241017194132100.png)

<img src="/home/ldh/.config/Typora/typora-user-images/image-20241017194154455.png" alt="image-20241017194154455" style="zoom:33%;" />

### 屏幕

![IMG20241017193441](/home/ldh/qq_data/IMG20241017193441.jpg)



![image-20241017203034386](/home/ldh/.config/Typora/typora-user-images/image-20241017203034386.png)

## 环境搭建

1. make menuconfig
   - 开启ntpd，gpio，wifi

2. 交叉编译opencv在tina linux中运行

   1. 安装cmake-gui

   2. 在cmake中配置，c选择riscv64-unknown-linux-gnu-gcc，c++选择riscv64-unknown-linux-gnu-g++

   3. 打开Advancd选项，按照下面内容设置

      ```
      CMAKE_BUILD_TYPE Release
      CMAKE_EXE_LINKER_FLAGS -ldl -lpthread -latomic
      CMAKE_INSTALL_PREFIX xxx/opencv/build/install(安装路径)
      
      关闭WITH_DC1394/WITH_1394 选项
      
      关闭jpeg 和 openjpeg 选项。
      关闭python和java所有相关选项
      
      勾选 opencv_world 将其全部链接成一个静态库
      ```

   4. 文件更改

      1. `riscv64-glibc-gcc-thead_20200702/sysroot/usr/include/features.h  ` 的第`364`行前 加 上` #define _FILE_OFFSET_BITS 64`
      2. `opencv/3rdparty/protobuf/src/google/protobuf/stubs/common.cc` 中增加一行`#define HAVE_PTHREAD`

   5. make && make install

   6. 将下面`riscv64-glibc-gcc-thead_20200702/riscv64-unknown-linux-gnu/lib64/lp64d/`中的文件上传到板子中

      1. `libopencv_world.so`（这个文件可能需改名为libopencv_world.so.4.5）
      2. `libatomic.so`
      3. `libatomic.so.1.2.0`
      4. `libatomic.so.1`

   7. 将`install`目录下的`bin`目录上传到板子

   8. 这样就可以交叉编译opencv相关的代码了

3. Tengine编译

   1. 将自己的代码放入`Tengine/example`下
   2. 修改example下的`CMakeLists.txt`，添加`TENGINE_EXAMPLE_CV (work_class   work_class.cpp)`
   3. 按照[官方](https://github.com/OAID/Tengine/tree/tengine-lite)编译即可，编译的时候注意指定上面交叉编译的opencv，例如`cmake -DCMAKE_TOOLCHAIN_FILE=../toolchains/rv64-c906.toolchain.cmake -DOpenCV_DIR=~/d1-h/opencv/build/install/lib/cmake/opencv4/ ..`

4. 打开hdmi输出

   ```shell
   cd /sys/kernel/debug/dispdbg
   echo disp0 > name
   echo switch1 > command
   echo 4 10 0 0 0x4 0x101 0 0 0 8 > param
   echo 1 > start;
   ```

5. 网页服务器

   1. 下载boa-0.94.13.tar.gz

   2. 在src目录执行`./configure --prefix=../../riscv-boa/ --host=riscv-linux`

   3. 修改生成的Makefile

      ```makefile
      CC = riscv64-unknown-linux-gnu-gcc
      CPP = riscv64-unknown-linux-gnu-gcc -E
      ```

   4. 修改compat.h文件120行为`#define TIMEZONE_OFFSET(foo) ((foo)->tm_gmtoff)`

   5. make

   6. 在d1-h的根目录执行

      ```shell
      cd /
      mkdir -p www/cgi-bin
      mkdir /etc/boa
      ```

   6. 将`index.html`上传到`/www`下，`pass.cgi`上传到`www/cgi-bin`下，`boa`上传到`/etc/boa`，`boa.conf`上传到`/etc/boa`。

   7. 将ubuntu的`/etc/mime.types`上传到`/etc`

   8. 连接wifi，在板子中执行`wifi_connect_ap_test ssid passwd`连接网络

   9. 执行下面命令，确保在板子上能ping通主机

      ```shell
      udhcpc -i br-lan
      udhcpc -i eth0 
      ```

   10. 在`/etc/boa`中执行`./boa`即可，在主机浏览器输入板子ip即可

6. 主程序执行
   1. `./work_class -m mobilenet_ssd.tmfile `
   2. 将Tengine中`build/install`目录下的`lib`目录上传，同时在板子中输入`export LD_LIBRARY_PATH=xxx/lib`rm ar
   3. `mobilenet_ssd.tmfile`在Tengine官方可以下载
   4. 模型识别效果并不理想，建议自己训练