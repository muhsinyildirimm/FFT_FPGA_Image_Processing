#include <windows.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/photo.hpp>
#include <iostream>
#include <vector>
#include <cstdint>
#include <string>
#include <cmath>

// ============================================================
//  PART 1: FPGA UART KONVOLUSYON FONKSIYONLARI
// ============================================================

bool processChannelOnFPGA(HANDLE hSerial, const cv::Mat& channel, cv::Mat& result)
{
    memcpy(result.ptr<uint8_t>(0), channel.ptr<uint8_t>(0), 256);
    memcpy(result.ptr<uint8_t>(255), channel.ptr<uint8_t>(255), 256);

    DWORD written = 0;
    WriteFile(hSerial, channel.ptr<uint8_t>(0), 256, &written, NULL);
    WriteFile(hSerial, channel.ptr<uint8_t>(1), 256, &written, NULL);
    WriteFile(hSerial, channel.ptr<uint8_t>(2), 256, &written, NULL);

    DWORD total_read = 0;
    std::vector<uint8_t> rx_line(256);
    while (total_read < 256) {
        DWORD read = 0;
        ReadFile(hSerial, rx_line.data() + total_read,
            256 - total_read, &read, NULL);
        if (read == 0) break;
        total_read += read;
    }
    if (total_read == 256) memcpy(result.ptr<uint8_t>(1), rx_line.data(), 256);

    for (int row = 3; row < 256; row++) {
        WriteFile(hSerial, channel.ptr<uint8_t>(row), 256, &written, NULL);
        total_read = 0;
        while (total_read < 256) {
            DWORD read = 0;
            ReadFile(hSerial, rx_line.data() + total_read,
                256 - total_read, &read, NULL);
            if (read == 0) break;
            total_read += read;
        }
        if (total_read == 256) {
            memcpy(result.ptr<uint8_t>(row - 1), rx_line.data(), 256);
        }
    }
    return true;
}

cv::Mat fpgaProcess(HANDLE hSerial, const cv::Mat& img, int kernelType)
{
    std::vector<cv::Mat> bgr_channels;
    cv::split(img, bgr_channels);

    std::vector<cv::Mat> output_channels(3);
    const char* names[] = { "B", "G", "R" };

    for (int ch = 0; ch < 3; ch++) {
        std::cout << "  Kanal " << names[ch] << "... " << std::flush;
        cv::Mat result(256, 256, CV_8U);
        processChannelOnFPGA(hSerial, bgr_channels[ch], result);
        output_channels[ch] = result;
        std::cout << "OK\n";
    }

    cv::Mat final_output;
    cv::merge(output_channels, final_output);
    cv::medianBlur(final_output, final_output, 1);

    // Kernel'e gore farkli denoising parametreleri
    int h_val, hColor_val, templateSize, searchSize;
    switch (kernelType) {
    case 0:  // Gaussian
        h_val = 5;  hColor_val = 5;  templateSize = 7;  searchSize = 21;
        break;
    case 1:  // Sharpening (en iyi sonuc 5-10-7-21)
        h_val = 5;  hColor_val = 10; templateSize = 7;  searchSize = 21;
        break;
    case 2:  // Edge Detection
        h_val = 5;  hColor_val = 5;  templateSize = 7;  searchSize = 21;
        break;
    case 3:  // Emboss (en guclu denoising)
        h_val = 25; hColor_val = 25; templateSize = 7;  searchSize = 21;
        break;
    default:
        h_val = 10; hColor_val = 10; templateSize = 7;  searchSize = 21;
        break;
    }

    cv::Mat denoised;
    cv::fastNlMeansDenoisingColored(final_output, denoised,
        h_val, hColor_val, templateSize, searchSize);
    return denoised;
}

// ============================================================
//  PART 2: PC TARAFINDA FFT/DFT TABANLI GORUNTU FILTRELEME
// ============================================================

void swapQuadrants(cv::Mat& mat)
{
    int cx = mat.cols / 2;
    int cy = mat.rows / 2;
    cv::Mat q0(mat, cv::Rect(0, 0, cx, cy));
    cv::Mat q1(mat, cv::Rect(cx, 0, cx, cy));
    cv::Mat q2(mat, cv::Rect(0, cy, cx, cy));
    cv::Mat q3(mat, cv::Rect(cx, cy, cx, cy));
    cv::Mat tmp;
    q0.copyTo(tmp); q3.copyTo(q0); tmp.copyTo(q3);
    q1.copyTo(tmp); q2.copyTo(q1); tmp.copyTo(q2);
}

// FFT filtre tipleri:
// 0 = Low-Pass
// 1 = High-Pass
// 2 = Band-Pass
// 3 = Notch (band-stop)
// 4 = Gaussian Low-Pass (yumusak gecisli)
// 5 = Gaussian High-Pass
// 6 = Emboss-benzeri (HP + bias)
cv::Mat fftFilterChannel(const cv::Mat& channel, int filterType,
    double radius1, double radius2 = 0.0)
{
    cv::Mat floatImg;
    channel.convertTo(floatImg, CV_32F);

    int m = cv::getOptimalDFTSize(channel.rows);
    int n = cv::getOptimalDFTSize(channel.cols);

    cv::Mat padded;
    cv::copyMakeBorder(floatImg, padded,
        0, m - channel.rows, 0, n - channel.cols,
        cv::BORDER_CONSTANT, cv::Scalar::all(0));

    cv::Mat planeArr[2];
    planeArr[0] = padded.clone();
    planeArr[1] = cv::Mat::zeros(padded.size(), CV_32F);
    cv::Mat spectrum;
    cv::merge(planeArr, 2, spectrum);
    cv::dft(spectrum, spectrum);

    int rows = spectrum.rows;
    int cols = spectrum.cols;
    int cx = cols / 2;
    int cy = rows / 2;

    cv::Mat mask = cv::Mat::zeros(rows, cols, CV_32F);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            double dist = std::sqrt((r - cy) * (r - cy) + (c - cx) * (c - cx));
            float val = 0.0f;
            switch (filterType) {
            case 0: val = (dist <= radius1) ? 1.0f : 0.0f; break;  // LP
            case 1: val = (dist > radius1) ? 1.0f : 0.0f; break;  // HP
            case 2: val = (dist >= radius1 && dist <= radius2) ? 1.0f : 0.0f; break;  // BP
            case 3: val = (dist <= radius1 || dist >= radius2) ? 1.0f : 0.0f; break;  // Notch
            case 4: val = std::exp(-(dist * dist) / (2.0 * radius1 * radius1)); break;  // Gaussian LP
            case 5: val = 1.0f - (float)std::exp(-(dist * dist) / (2.0 * radius1 * radius1)); break;  // Gaussian HP
            case 6: // Emboss-benzeri: HP + faz/genlik manipulasyonu
                val = (dist > radius1) ? 1.5f : 0.3f;
                break;
            default: val = 1.0f;
            }
            mask.at<float>(r, c) = val;
        }
    }

    swapQuadrants(mask);

    std::vector<cv::Mat> ch;
    cv::split(spectrum, ch);
    ch[0] = ch[0].mul(mask);
    ch[1] = ch[1].mul(mask);
    cv::Mat filtered;
    cv::merge(ch, filtered);

    cv::Mat result;
    cv::dft(filtered, result, cv::DFT_INVERSE | cv::DFT_REAL_OUTPUT);
    cv::Mat cropped = result(cv::Rect(0, 0, channel.cols, channel.rows)).clone();

    cv::Mat output;
    cv::normalize(cropped, output, 0, 255, cv::NORM_MINMAX);
    output.convertTo(output, CV_8U);
    return output;
}

cv::Mat fftProcess(const cv::Mat& img, int filterType,
    double radius1, double radius2 = 0.0)
{
    std::vector<cv::Mat> bgr_channels;
    cv::split(img, bgr_channels);

    std::vector<cv::Mat> output_channels(3);
    for (int ch = 0; ch < 3; ch++) {
        output_channels[ch] = fftFilterChannel(bgr_channels[ch],
            filterType, radius1, radius2);
    }

    cv::Mat final_output;
    cv::merge(output_channels, final_output);
    return final_output;
}

cv::Mat createSpectrum(const cv::Mat& img)
{
    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    cv::Mat floatImg;
    gray.convertTo(floatImg, CV_32F);

    cv::Mat planeArr[2];
    planeArr[0] = floatImg.clone();
    planeArr[1] = cv::Mat::zeros(floatImg.size(), CV_32F);
    cv::Mat spectrum;
    cv::merge(planeArr, 2, spectrum);
    cv::dft(spectrum, spectrum);

    std::vector<cv::Mat> planes;
    cv::split(spectrum, planes);
    cv::Mat magnitude;
    cv::magnitude(planes[0], planes[1], magnitude);
    magnitude += cv::Scalar::all(1);
    cv::log(magnitude, magnitude);

    swapQuadrants(magnitude);
    cv::Mat spectrumImg;
    cv::normalize(magnitude, spectrumImg, 0, 255, cv::NORM_MINMAX);
    spectrumImg.convertTo(spectrumImg, CV_8U);

    cv::Mat colorSpec;
    cv::cvtColor(spectrumImg, colorSpec, cv::COLOR_GRAY2BGR);
    return colorSpec;
}

// ============================================================
//  YARDIMCI: COM port ac
// ============================================================
HANDLE openSerial(const char* portName)
{
    HANDLE hSerial = CreateFileA(portName,
        GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    if (hSerial == INVALID_HANDLE_VALUE) return NULL;

    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(dcb);
    GetCommState(hSerial, &dcb);
    dcb.BaudRate = 115200;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    SetCommState(hSerial, &dcb);

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 100;
    timeouts.ReadTotalTimeoutConstant = 15000;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hSerial, &timeouts);

    return hSerial;
}

// 5'li karsilastirma gorseli
cv::Mat createComparison5(const cv::Mat& original,
    const cv::Mat& a, const cv::Mat& b,
    const cv::Mat& c, const cv::Mat& d,
    const std::string& la, const std::string& lb,
    const std::string& lc, const std::string& ld)
{
    int w = 256, h = 256, margin = 10, label_h = 30;
    cv::Mat canvas(h + label_h + margin * 2, w * 5 + margin * 6, CV_8UC3,
        cv::Scalar(40, 40, 40));

    auto place = [&](const cv::Mat& img, int idx, const std::string& label) {
        int x = margin + idx * (w + margin);
        int y = margin + label_h;
        cv::Mat resized;
        if (img.size() != cv::Size(w, h)) cv::resize(img, resized, cv::Size(w, h));
        else resized = img;
        if (resized.channels() == 1) cv::cvtColor(resized, resized, cv::COLOR_GRAY2BGR);
        resized.copyTo(canvas(cv::Rect(x, y, w, h)));
        cv::putText(canvas, label, cv::Point(x + 10, margin + 22),
            cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 1);
        };

    place(original, 0, "Orijinal");
    place(a, 1, la);
    place(b, 2, lb);
    place(c, 3, lc);
    place(d, 4, ld);
    return canvas;
}

// 9'lu karsilastirma gorseli (hibrit icin: 1 orijinal + 4 FFT + 4 FPGA)
cv::Mat createHybridComparison(const cv::Mat& original,
    const cv::Mat& fft_lp, const cv::Mat& fft_hp,
    const cv::Mat& fft_edge, const cv::Mat& fft_emboss,
    const cv::Mat& fpga_g, const cv::Mat& fpga_s,
    const cv::Mat& fpga_e, const cv::Mat& fpga_em)
{
    int w = 220, h = 220, margin = 8, label_h = 25;
    int cols = 5;
    int rows = 2;

    cv::Mat canvas(h * rows + label_h * rows + margin * (rows + 2),
        w * cols + margin * (cols + 1), CV_8UC3,
        cv::Scalar(40, 40, 40));

    // Üst sıra: FFT sonuçları
    auto placeRow = [&](const cv::Mat& img, int rowIdx, int colIdx,
        const std::string& label) {
            int x = margin + colIdx * (w + margin);
            int y = margin + rowIdx * (h + label_h + margin) + label_h;
            cv::Mat resized;
            if (img.size() != cv::Size(w, h)) cv::resize(img, resized, cv::Size(w, h));
            else resized = img;
            if (resized.channels() == 1) cv::cvtColor(resized, resized, cv::COLOR_GRAY2BGR);
            resized.copyTo(canvas(cv::Rect(x, y, w, h)));
            cv::putText(canvas, label,
                cv::Point(x + 5, y - 8),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
        };

    // Üst sıra: Orijinal + 4 FFT
    placeRow(original, 0, 0, "Orijinal");
    placeRow(fft_lp, 0, 1, "FFT-LP");
    placeRow(fft_hp, 0, 2, "FFT-HP");
    placeRow(fft_edge, 0, 3, "FFT-Edge");
    placeRow(fft_emboss, 0, 4, "FFT-Emboss");

    // Alt sıra: Orijinal + 4 FPGA
    placeRow(original, 1, 0, "Orijinal");
    placeRow(fpga_g, 1, 1, "FPGA-Gauss");
    placeRow(fpga_s, 1, 2, "FPGA-Sharp");
    placeRow(fpga_e, 1, 3, "FPGA-Edge");
    placeRow(fpga_em, 1, 4, "FPGA-Emboss");

    return canvas;
}

// ============================================================
//  ANA MENU
// ============================================================
int main()
{
    std::string inputPath = "C:/Users/muhsi/OneDrive/Desktop/test_image.jpg";
    std::string desktop = "C:/Users/muhsi/OneDrive/Desktop/";

    cv::Mat img = cv::imread(inputPath, cv::IMREAD_COLOR);
    if (img.empty()) {
        std::cout << "HATA: Resim acilamadi -> " << inputPath << "\n";
        system("pause");
        return -1;
    }
    cv::resize(img, img, cv::Size(256, 256));
    std::cout << "Goruntu hazir: 256x256 renkli\n\n";

    std::cout << "==========================================\n";
    std::cout << "    GORUNTU ISLEME SISTEMI (HIBRIT)\n";
    std::cout << "==========================================\n";
    std::cout << "Hangi yontem kullanilsin?\n\n";
    std::cout << "  1 - FPGA Konvolusyon (Spatial Domain)\n";
    std::cout << "      4 kernel: Gaussian, Sharpening, Edge, Emboss\n";
    std::cout << "  2 - FFT/DFT Filtreleme (Frequency Domain)\n";
    std::cout << "      LP, HP, Band-pass, Notch\n";
    std::cout << "  3 - Hibrit Karsilastirma (her ikisi tam)\n";
    std::cout << "      4 FFT + 4 FPGA (Gauss-LP, Sharp-HP, Edge, Emboss)\n";
    std::cout << "==========================================\n";
    std::cout << "Seciminiz: ";

    int choice;
    std::cin >> choice;
    std::cin.ignore();

    // ============================================================
    // SECIM 1: FPGA Konvolusyon
    // ============================================================
    if (choice == 1) {
        HANDLE hSerial = openSerial("\\\\.\\COM6");
        if (!hSerial) {
            std::cout << "HATA: COM6 acilamadi\n";
            system("pause");
            return -1;
        }

        std::cout << "\n--- FPGA Konvolusyon (4 Kernel Otomatik) ---\n";

        struct K { const char* name; const char* sw; const char* file; };
        K tests[4] = {
            { "Gaussian Blur",  "SW1=0 SW0=0", "fpga_gaussian.png"   },
            { "Sharpening",     "SW1=0 SW0=1", "fpga_sharpening.png" },
            { "Edge Detection", "SW1=1 SW0=0", "fpga_edge.png"       },
            { "Emboss",         "SW1=1 SW0=1", "fpga_emboss.png"     }
        };
        std::vector<cv::Mat> results;

        for (int i = 0; i < 4; i++) {
            std::cout << "\nTest " << (i + 1) << "/4: " << tests[i].name << "\n";
            std::cout << "Switch: " << tests[i].sw << ", BTNC reset, ENTER...";
            std::cin.get();
            cv::Mat r = fpgaProcess(hSerial, img, i);
            cv::imwrite(desktop + tests[i].file, r);
            results.push_back(r);
        }
        CloseHandle(hSerial);

        cv::Mat comp = createComparison5(img, results[0], results[1],
            results[2], results[3],
            "Gaussian", "Sharpen", "Edge", "Emboss");
        cv::imwrite(desktop + "fpga_comparison.png", comp);
        cv::imshow("FPGA Konvolusyon Karsilastirma", comp);
        cv::waitKey(0);
    }

    // ============================================================
    // SECIM 2: FFT/DFT Filtreleme
    // ============================================================
    else if (choice == 2) {
        std::cout << "\n--- FFT/DFT Tabanli Filtreleme ---\n";
        std::cout << "Spektrum hesaplaniyor...\n";

        cv::Mat spectrum = createSpectrum(img);
        cv::imwrite(desktop + "fft_spectrum.png", spectrum);

        std::cout << "Filtreler uygulaniyor...\n";

        cv::Mat lp = fftProcess(img, 0, 30.0);            // LP
        cv::Mat hp = fftProcess(img, 1, 15.0);            // HP
        cv::Mat bp = fftProcess(img, 2, 15.0, 50.0);      // BP
        cv::Mat notch = fftProcess(img, 3, 5.0, 80.0);       // Notch

        cv::imwrite(desktop + "fft_lowpass.png", lp);
        cv::imwrite(desktop + "fft_highpass.png", hp);
        cv::imwrite(desktop + "fft_bandpass.png", bp);
        cv::imwrite(desktop + "fft_notch.png", notch);

        cv::Mat comp = createComparison5(img, lp, hp, bp, notch,
            "Low-Pass", "High-Pass",
            "Band-Pass", "Notch");
        cv::imwrite(desktop + "fft_comparison.png", comp);

        std::cout << "Tum FFT sonuclari masaustune kaydedildi.\n";
        cv::imshow("FFT Filtreleme Karsilastirma", comp);
        cv::waitKey(0);
    }

    // ============================================================
    // SECIM 3: Hibrit Karsilastirma (4 FFT + 4 FPGA)
    // ============================================================
    else if (choice == 3) {
        std::cout << "\n--- HIBRIT KARSILASTIRMA (4+4) ---\n";

        // FFT Filtreleri (CPU)
        std::cout << "FFT filtreleri uygulaniyor (CPU)...\n";
        cv::Mat fftLP = fftProcess(img, 4, 30.0);    // Gaussian LP (yumusatma)
        cv::Mat fftHP = fftProcess(img, 5, 15.0);    // Gaussian HP (keskinlestirme)
        cv::Mat fftEdge = fftProcess(img, 1, 8.0);     // Sert HP (kenar)
        cv::Mat fftEmboss = fftProcess(img, 6, 20.0);    // Emboss-benzeri

        cv::imwrite(desktop + "hybrid_fft_lp.png", fftLP);
        cv::imwrite(desktop + "hybrid_fft_hp.png", fftHP);
        cv::imwrite(desktop + "hybrid_fft_edge.png", fftEdge);
        cv::imwrite(desktop + "hybrid_fft_emboss.png", fftEmboss);
        std::cout << "4 FFT filtresi tamam.\n";

        // FPGA Konvolusyonlari
        HANDLE hSerial = openSerial("\\\\.\\COM6");
        if (!hSerial) {
            std::cout << "HATA: COM6 acilamadi\n";
            system("pause");
            return -1;
        }

        struct K { const char* name; const char* sw; const char* file; int type; };
        K tests[4] = {
            { "Gaussian",   "SW1=0 SW0=0", "hybrid_fpga_gauss.png",  0 },
            { "Sharpening", "SW1=0 SW0=1", "hybrid_fpga_sharp.png",  1 },
            { "Edge",       "SW1=1 SW0=0", "hybrid_fpga_edge.png",   2 },
            { "Emboss",     "SW1=1 SW0=1", "hybrid_fpga_emboss.png", 3 }
        };

        std::vector<cv::Mat> fpgaResults;
        for (int i = 0; i < 4; i++) {
            std::cout << "\nFPGA Test " << (i + 1) << "/4: " << tests[i].name << "\n";
            std::cout << "Switch: " << tests[i].sw << ", BTNC reset, ENTER...";
            std::cin.get();
            cv::Mat r = fpgaProcess(hSerial, img, tests[i].type);
            cv::imwrite(desktop + tests[i].file, r);
            fpgaResults.push_back(r);
        }
        CloseHandle(hSerial);

        // 9'lu karsilastirma gorseli
        cv::Mat hybridComp = createHybridComparison(img,
            fftLP, fftHP, fftEdge, fftEmboss,
            fpgaResults[0], fpgaResults[1], fpgaResults[2], fpgaResults[3]);
        cv::imwrite(desktop + "hybrid_comparison.png", hybridComp);

        std::cout << "\nHibrit karsilastirma tamamlandi.\n";
        std::cout << "9 goruntu: 1 orijinal + 4 FFT + 4 FPGA\n";
        cv::imshow("Hibrit Karsilastirma (FFT vs FPGA Konvolusyon)", hybridComp);
        cv::waitKey(0);
    }

    return 0;
}
