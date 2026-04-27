[TR]
FPGA ve OpenCv kullanılarak main.cpp dosyası ile istenilen görseli 4 farklı modda iyileştirme çalışması.
1-Gaussian Blur | 2-Sharpening | 3-Edge Detection | 4-Emboss
Bu 4 farklı modu Nexys DDR4 FPGA kartı üzerinde vivado ile programlanıp, C++ ile görüntü alışverişi UART ile sağlanmıştır. Board üzerindeki anahtarlar (switch-SW) vasıtasıyla modlar arasında geçiş yapılır. 
Program baaşarıyla çalışıktan sonra orijinal görüntü ve işlem sonrası görüntü yan yana gösterilip karşılaştırma yapılır. 
Amaç sinyal işleme konusunda yer alan FFT,DFT ve konvolüsyon tekniklerini kullanarak FPGA'nin karmaşık işlem yeteneğinden faydalanılıp görüntü işleme yapmaktır. 
***********************************************************************************
[EN]
Image enhancement of a target image in four different modes using FPGA and OpenCV via the main.cpp file.
1-Gaussian Blur | 2-Sharpening | 3-Edge Detection | 4-Emboss
These four modes were programmed on a Nexys DDR4 FPGA board using Vivado, and image exchange was facilitated via UART using C++. Switching between modes is performed using the switches (SW) on the board. 
After the program runs successfully, the original image and the processed image are displayed side by side for comparison. 
The objective is to perform image processing by leveraging the FPGA’s complex processing capabilities using signal processing techniques such as FFT, DFT, and convolution. 
