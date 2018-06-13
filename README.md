cd /root/srtest
youtube-dl -f 133 https://www.youtube.com/watch?v=8OY8HFGsbKM
mv SRGAN_Super-resolved_video-8OY8HFGsbKM.mp4 videos/test.mp4
python3 supervideo.py
g++ -std=c++11 -I/usr/include -L/usr/lib "contrastAdj.cpp" -lopencv_core -lopencv_highgui -lopencv_imgproc -lopencv_videoio -lopencv_imgcodecs -lavcodec -lavformat -lavutil -lswscale -o contrastAdj.out
./contrastAdj.out
