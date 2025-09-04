# HSP004A_push
  
机器狗已部署的rtsp推流：
sudo gst-launch-1.0 v4l2src device=/dev/video0 ! image/jpeg, width=1280, height=720, framerate=30/1 ! jpegdec ! videoconvert ! mpph265enc rc-mode=0 bps=1800000 bps-max=2000000 qp-min=12 qp-max=45 ! rtspclientsink location=rtsp://127.0.0.1:8554/video1 latency=10 
  
现在用代码的方式在中间去畸变，然后RTSP推流