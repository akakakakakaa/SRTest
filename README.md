1. cd /root/srtest
2. youtube-dl -f 133 https://www.youtube.com/watch?v=8OY8HFGsbKM
3. mv SRGAN_Super-resolved_video-8OY8HFGsbKM.mp4 videos/test.mp4
4. python3 supervideo.py
5. g++ -std=c++11 -I/usr/include -L/usr/lib "contrastAdj.cpp" -lopencv_core -lopencv_highgui -lopencv_imgproc -lopencv_videoio -lopencv_imgcodecs -lavcodec -lavformat -lavutil -lswscale -o contrastAdj.out
6. ./contrastAdj.out
