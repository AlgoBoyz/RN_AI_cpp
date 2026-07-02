@echo off
chcp 65001 > nul
echo ============================================
echo  Frame Sender - 发送屏幕采集到 192.168.137.2
echo ============================================
echo.
echo 默认发送到本机网口 192.168.137.2:5000
echo 如需修改，请编辑此批处理文件
echo.
echo 按 Ctrl+C 停止发送
echo ============================================
echo.

frame_sender.exe --host 192.168.137.2 --port 5000 --monitor 0 --crop-size 640 --jpeg-quality 80

pause
