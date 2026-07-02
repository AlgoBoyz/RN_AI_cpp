Frame Sender - 使用说明
========================

用途：将本机屏幕采集并编码为 JPEG，通过 UDP 发送到目标机器。

网络配置
--------
发送端（本包）：    192.168.137.1
接收端（目标机）： 192.168.137.2

使用方法
--------
1. 确保两台机器通过网线直连，IP 已设为 192.168.137.x
2. 在接收端（192.168.137.2）运行接收程序
3. 双击 run_sender.bat 即可开始发送

高级参数（修改 run_sender.bat）：
  --monitor 0        采集的显示器编号（0=主屏）
  --crop-size 640    裁切尺寸（从画面中央裁出正方形）
  --jpeg-quality 80  JPEG 质量 (1-100)

文件清单
--------
frame_sender.exe        - 主程序
run_sender.bat          - 启动脚本
opencv_world4100.dll    - OpenCV 运行库
cudart64_12.dll         - CUDA 运行时
msvcp140*.dll           - VC++ 运行库
vcruntime140*.dll       - VC++ 运行库
