#include <Wire.h>               // Library untuk komunikasi I2C
#include <LiquidCrystal_I2C.h>  // Library untuk LCD dengan interface I2C
#include <WiFi.h>               // Library untuk koneksi WiFi pada ESP32
#include <RTClib.h>             // Library untuk modul Real Time Clock
#include <NTPClient.h>          // Library untuk sinkronisasi waktu via NTP
#include <WiFiUdp.h>            // Library untuk protokol UDP (digunakan oleh NTP)
#include <HTTPClient.h>         // Library untuk melakukan HTTP request
#include <ArduinoJson.h>        // Library untuk memproses data JSON
#include <Ultrasonic.h>         // Library untuk sensor ultrasonik
#include <Adafruit_ADS1X15.h>   // Library untuk modul ADS1115 (ADC eksternal)
#include <WiFiManager.h>        // Library untuk manajemen koneksi WiFi
#include <Preferences.h>        // Library untuk penyimpanan data non-volatile

// Inisialisasi objek untuk komponen utama
Adafruit_ADS1115 ads;                             // Objek untuk ADS1115 (ADC 16-bit)
WiFiUDP ntpUDP;                                   // Objek UDP untuk NTP
NTPClient timeClient(ntpUDP, "time.google.com");  // Objek NTP client server Indonesia
RTC_DS3231 rtc;                                   // Objek untuk modul RTC DS3231
LiquidCrystal_I2C lcd(0x27, 20, 4);               // Objek LCD 20x4 dengan alamat I2C 0x27
WiFiManager manajerWifi;                          // Objek untuk manajemen WiFi
Preferences preferensi;                           // Objek untuk penyimpanan preferensi

// Konfigurasi Firebase
const String urlFirebase = "https://ujicoba2025-71e98-default-rtdb.asia-southeast1.firebasedatabase.app/";
// const String urlFirebase = "https://fishfeeder-jonny-default-rtdb.asia-southeast1.firebasedatabase.app/";

// Definisi pin untuk sensor ultrasonic pakan
#define PIN_TRIGGER_PAKAN 33  // Pin trigger untuk sensor ultrasonic pakan
#define PIN_ECHO_PAKAN 32     // Pin echo untuk sensor ultrasonic pakan

// Definisi pin untuk sensor ultrasonic kolam
#define PIN_TRIGGER_KOLAM 26  // Pin trigger untuk sensor ultrasonic kolam
#define PIN_ECHO_KOLAM 25     // Pin echo untuk sensor ultrasonic kolam

// Inisialisasi sensor ultrasonic untuk pakan
Ultrasonic sensorPakan(PIN_TRIGGER_PAKAN, PIN_ECHO_PAKAN);

// Definisi pin untuk relay dan LED
#define RELAY_PAKAN 14  // Pin relay untuk pemberian pakan
#define RELAY_BUANG 12  // Pin relay untuk pembuangan air
#define RELAY_ISI 13    // Pin relay untuk pengisian air
#define PIN_LED 2       // Pin LED indikator
#define PIN_RESET 15    // Pin untuk tombol reset

// Variabel untuk menyimpan waktu
DateTime waktuSekarang;  // Variabel untuk menyimpan waktu saat ini

// Variabel untuk sensor kekeruhan
int nilaiKekeruhan = 0;  // Nilai kekeruhan air

// Konstanta dan variabel untuk level air
const int LEVEL_AIR_MIN = 40;  // Level air minimum (dalam cm)
const int LEVEL_AIR_MAX = 20;  // Level air maksimum (dalam cm)
int batasKekeruhan = 30;       // Batas nilai kekeruhan (dapat diubah melalui Firebase)

// Konstanta dan variabel untuk pakan
const float TINGGI_WADAH_PAKAN = 25.0;  // Tinggi wadah pakan (dalam cm)
const int STOK_PAKAN_PENUH = 4000;      // Stok pakan penuh (dalam gram)
int stokPakan = 0;                      // Stok pakan saat ini (dalam gram)
int beratPakanPerPemberian = 0;         // Berat pakan setiap kali pemberian (dalam gram)
int waktuPerGram = 1000;                // Waktu untuk memberikan 1 gram pakan (dalam ms)

// Variabel untuk waktu terakhir update
unsigned long waktuUpdateTerakhir = 0;          // Waktu terakhir jadwal diperbarui
unsigned long waktuUpdateFirebaseTerakhir = 0;  // Waktu terakhir update data dari Firebase
const unsigned long INTERVAL_UPDATE = 60000;    // Interval update (60000 ms = 1 menit)

// Status untuk pengurasan
enum StatusPengurasan {
  MEMBUANG,  // Sedang membuang air
  MENGISI,   // Sedang mengisi air
  SELESAI    // Pengurasan selesai
};
StatusPengurasan statusPengurasan = SELESAI;

// Struktur untuk menyimpan jadwal pemberian pakan
struct Jadwal {
  int jam;
  int menit;
};

// Array dinamis untuk menyimpan jadwal dan status pemberian pakan
Jadwal* jadwalPemberian = nullptr;
int jumlahJadwal = 0;
bool* statusPemberianPakan = nullptr;  // Untuk menandai apakah pakan sudah diberikan

// Fungsi setup: dijalankan sekali saat Arduino mulai
void setup() {
  Serial.begin(115200);  // Inisialisasi komunikasi serial dengan baud rate 115200
  Wire.begin();          // Inisialisasi komunikasi I2C

  // Inisialisasi LCD
  lcd.init();       // Inisialisasi LCD
  lcd.backlight();  // Nyalakan backlight LCD
  lcd.clear();      // Bersihkan tampilan LCD

  // Inisialisasi pin LED
  pinMode(PIN_LED, OUTPUT);

  // Tampilkan pesan koneksi WiFi
  lcd.setCursor(0, 0);
  lcd.print("Menghubungkan WiFi...");

  // Berkedip LED saat tombol reset ditekan
  for (int i = 0; i < 10; i++) {
    digitalWrite(PIN_LED, HIGH);
    delay(200);
    digitalWrite(PIN_LED, LOW);
    delay(200);
    digitalWrite(PIN_LED, HIGH);
  }

  // Coba koneksi WiFi otomatis
  if (!manajerWifi.autoConnect("ElinFish", "12345678")) {
    Serial.println("Gagal terhubung ke WiFi, masuk ke mode AP...");
  }

  // Pastikan ESP32 terhubung ke WiFi sebelum melanjutkan
  while (!WiFi.isConnected()) {
    delay(100);  // Tunggu hingga koneksi berhasil
  }

  Serial.println("WiFi terhubung");
  Serial.print("Alamat IP: ");
  Serial.println(WiFi.localIP());

  // Inisialisasi RTC
  if (!rtc.begin()) {
    Serial.println("RTC tidak terdeteksi");
    while (1)
      ;  // Berhenti jika RTC tidak terdeteksi
  }

  // Cek koneksi ADS1115
  if (!ads.begin()) {
    Serial.println("Gagal menghubungkan ke ADS1115. Periksa koneksi!");
    while (1)
      ;  // Berhenti jika ADS1115 tidak terdeteksi
  }

  // Inisialisasi pin relay dan LED
  digitalWrite(PIN_LED, LOW);
  pinMode(RELAY_PAKAN, OUTPUT);
  digitalWrite(RELAY_PAKAN, LOW);
  pinMode(RELAY_ISI, OUTPUT);
  digitalWrite(RELAY_ISI, LOW);
  pinMode(RELAY_BUANG, OUTPUT);
  digitalWrite(RELAY_BUANG, LOW);

  // Inisialisasi sensor ultrasonic untuk pakan
  pinMode(PIN_TRIGGER_PAKAN, OUTPUT);
  pinMode(PIN_ECHO_PAKAN, INPUT);

  // Inisialisasi sensor ultrasonic untuk kolam
  pinMode(PIN_TRIGGER_KOLAM, OUTPUT);
  pinMode(PIN_ECHO_KOLAM, INPUT);

  // Inisialisasi pin reset
  pinMode(PIN_RESET, INPUT_PULLUP);

  // Baca data dari penyimpanan non-volatile
  preferensi.begin("ikanpakan", false);

  // Baca batas kekeruhan dari penyimpanan
  batasKekeruhan = preferensi.getInt("batasKekeruhan", 30);  // Default 30 jika tidak ada
  Serial.print("Batas Kekeruhan dari penyimpanan: ");
  Serial.println(batasKekeruhan);

  // Baca jumlah jadwal dari penyimpanan
  jumlahJadwal = preferensi.getInt("jumlahJadwal", 0);
  if (jumlahJadwal > 0) {
    jadwalPemberian = new Jadwal[jumlahJadwal];
    statusPemberianPakan = new bool[jumlahJadwal]();

    // Baca setiap jadwal dari penyimpanan
    for (int i = 0; i < jumlahJadwal; i++) {
      jadwalPemberian[i].jam = preferensi.getInt(("jam" + String(i)).c_str(), 0);
      jadwalPemberian[i].menit = preferensi.getInt(("menit" + String(i)).c_str(), 0);
    }

    // Baca berat pakan per pemberian
    beratPakanPerPemberian = preferensi.getInt("beratPakan", 0);
    Serial.println("Data jadwal dan pakan berhasil dibaca dari penyimpanan");
  } else {
    Serial.println("Tidak ada data jadwal dan pakan di penyimpanan");
  }
  preferensi.end();

  // Ambil jadwal dan batas kekeruhan dari Firebase
  ambilJadwalPakan();
  ambilBatasKekeruhan();

  // Update RTC dari NTP (server waktu online)
  updateRTCdariNTP();
}

// Fungsi loop: dijalankan berulang-ulang
void loop() {
  // Cek status WiFi dan tombol reset
  if (digitalRead(PIN_RESET) == LOW || WiFi.status() != WL_CONNECTED) {
    modePengaturanWifi();
  }

  // Dapatkan waktu dari RTC
  waktuSekarang = rtc.now();

  // Hitung stok pakan dan cek kekeruhan
  stokPakan = hitungStokPakan();
  nilaiKekeruhan = cekNilaiKekeruhan();

  // Perbarui data dari Firebase setiap 1 menit
  if (millis() - waktuUpdateFirebaseTerakhir >= 60000) {
    ambilJadwalPakan();
    ambilBatasKekeruhan();
    waktuUpdateFirebaseTerakhir = millis();
  }

  // Tampilkan informasi pada LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Waktu: ");
  lcd.print(waktuSekarang.hour());
  lcd.print(":");
  if (waktuSekarang.minute() < 10) lcd.print("0");
  lcd.print(waktuSekarang.minute());
  lcd.print(":");
  if (waktuSekarang.second() < 10) lcd.print("0");
  lcd.print(waktuSekarang.second());

  lcd.setCursor(0, 1);
  lcd.print("Stok: ");
  lcd.print(stokPakan);
  lcd.print(" gram");

  lcd.setCursor(0, 2);
  lcd.print("Kekeruhan: ");
  lcd.print(nilaiKekeruhan);
  lcd.print(" NTU");

  // Tampilkan jadwal pemberian pakan terdekat
  Jadwal jadwalTerdekat = cariJadwalTerdekat();
  if (jadwalTerdekat.jam != -1 && jadwalTerdekat.menit != -1) {
    lcd.setCursor(0, 3);
    lcd.print("Berikutnya: ");
    lcd.print(jadwalTerdekat.jam);
    lcd.print(":");
    if (jadwalTerdekat.menit < 10) {
      lcd.print("0");  // Tambahkan "0" di depan untuk menit < 10
    }
    lcd.print(jadwalTerdekat.menit);
  } else {
    lcd.setCursor(0, 3);
    lcd.print("Tidak ada jadwal");
  }

  // Cek kekeruhan air dan lakukan pengurasan jika perlu
  if (nilaiKekeruhan > batasKekeruhan && statusPengurasan == SELESAI) {
    laksanakanPengurasan();
  }

  // Cek apakah sudah waktunya memperbarui jadwal
  if (millis() - waktuUpdateTerakhir > INTERVAL_UPDATE) {
    waktuUpdateTerakhir = millis();

    // Cek jadwal pemberian pakan
    for (int i = 0; i < jumlahJadwal; i++) {
      if (waktuSekarang.hour() == jadwalPemberian[i].jam && waktuSekarang.minute() == jadwalPemberian[i].menit) {
        if (!statusPemberianPakan[i]) {
          berikanPakan();
          statusPemberianPakan[i] = true;
        }
      } else {
        statusPemberianPakan[i] = false;  // Reset status jika waktu tidak sesuai
      }
    }
  }

  delay(1000);  // Delay 1 detik untuk mengurangi beban CPU dan menghindari pembacaan terlalu cepat
}

// Fungsi untuk mengambil batas kekeruhan dari Firebase
void ambilBatasKekeruhan() {
  HTTPClient http;
  String respons;
  DynamicJsonDocument dokumen(1024);

  http.begin(urlFirebase + "pengurasan/batasKekeruhan.json");
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    respons = http.getString();
    DeserializationError kesalahan = deserializeJson(dokumen, respons);

    if (kesalahan) {
      Serial.println("Gagal mengurai JSON untuk batas kekeruhan");
      return;
    }

    int batasBaru = dokumen.as<int>();
    if (batasBaru > 0) {  // Pastikan nilai valid
      batasKekeruhan = batasBaru;
      Serial.print("Batas Kekeruhan: ");
      Serial.println(batasKekeruhan);

      // Simpan ke penyimpanan
      preferensi.begin("ikanpakan", false);
      int nilaiTersimpan = preferensi.getInt("batasKekeruhan", -1);
      if (nilaiTersimpan != batasKekeruhan) {
        preferensi.putInt("batasKekeruhan", batasKekeruhan);
        Serial.println("Batas kekeruhan disimpan ke penyimpanan");
      } else {
        Serial.println("Nilai batasKekeruhan sama, tidak disimpan ulang.");
      }
      preferensi.end();
    }
  } else {
    Serial.println("Gagal mengambil batas kekeruhan dari Firebase");
  }

  http.end();
}

// Fungsi untuk mencari jadwal pemberian pakan terdekat
Jadwal cariJadwalTerdekat() {
  Jadwal jadwalTerdekat = { -1, -1 };  // Inisialisasi dengan nilai default
  DateTime waktuSekarang = rtc.now();  // Waktu saat ini

  int selisihTerdekat = INT_MAX;  // Selisih waktu terdekat

  for (int i = 0; i < jumlahJadwal; i++) {
    int selisihMenit = (jadwalPemberian[i].jam - waktuSekarang.hour()) * 60 + (jadwalPemberian[i].menit - waktuSekarang.minute());

    // Jika jadwal sudah lewat hari ini, tambahkan 24 jam (1440 menit)
    if (selisihMenit < 0) {
      selisihMenit += 1440;
    }

    // Cari jadwal dengan selisih waktu terkecil
    if (selisihMenit < selisihTerdekat) {
      selisihTerdekat = selisihMenit;
      jadwalTerdekat = jadwalPemberian[i];
    }
  }

  return jadwalTerdekat;
}

// Fungsi untuk masuk ke mode pengaturan WiFi
void modePengaturanWifi() {
  Serial.println("Masuk ke Mode Pengaturan WiFi");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mode WiFi: ElinFish");
  lcd.setCursor(0, 1);
  lcd.print("Akses konfigurasi:");
  lcd.setCursor(0, 2);
  lcd.print("192.168.4.1");

  // Nyalakan LED berkedip saat masuk mode pengaturan
  for (int i = 0; i < 10; i++) {
    digitalWrite(PIN_LED, HIGH);
    delay(200);
    digitalWrite(PIN_LED, LOW);
    delay(200);
    digitalWrite(PIN_LED, HIGH);
  }

  delay(1000);                                         // Debounce untuk tombol
  manajerWifi.resetSettings();                         // Reset semua pengaturan WiFi tersimpan
  manajerWifi.startConfigPortal("ElinFish", "12345678");  // Mulai portal konfigurasi WiFi

  Serial.println("Konfigurasi selesai, memulai ulang ESP32.");
  ESP.restart();  // Mulai ulang ESP32 setelah konfigurasi
}

// Fungsi untuk melaksanakan pengurasan kolam
void laksanakanPengurasan() {
  Serial.println("Mulai pengurasan...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mulai pengurasan...");

  // Perbarui status pengurasan ke Firebase
  statusPengurasan = MEMBUANG;
  kirimStatusPengurasan("Pengurasan");

  // Mulai proses pembuangan air
  digitalWrite(RELAY_BUANG, HIGH);
  float levelSaatIni = dapatkanLevelAir();

  // Tunggu sampai air mencapai level minimum
  while (levelSaatIni <= LEVEL_AIR_MIN) {
    delay(1000);
    levelSaatIni = dapatkanLevelAir();
  }

  digitalWrite(RELAY_BUANG, LOW);
  Serial.println("Pengurasan selesai, mulai pengisian...");

  // Cek kekeruhan setelah pengurasan
  nilaiKekeruhan = cekNilaiKekeruhan(); 

  delay(2000);

  lcd.setCursor(0, 0);
  lcd.print("Mulai pengisian...");

  // Perbarui status pengisian ke Firebase
  statusPengurasan = MENGISI;
  kirimStatusPengurasan("Pengisian");

  // Mulai proses pengisian air
  digitalWrite(RELAY_ISI, HIGH);
  levelSaatIni = dapatkanLevelAir();

  // Tunggu sampai air mencapai level maksimum
  while (levelSaatIni >= LEVEL_AIR_MAX) {
    delay(1000);
    levelSaatIni = dapatkanLevelAir();
  }

  digitalWrite(RELAY_ISI, LOW);
  Serial.println("Pengisian selesai.");


  // Cek kekeruhan setelah pengisian
  nilaiKekeruhan = cekNilaiKekeruhan();

  // Reset status setelah beberapa detik
  delay(2000);
  // Perbarui status selesai ke Firebase
  statusPengurasan = SELESAI;
  kirimStatusPengurasan("Selesai");
}

// Fungsi untuk mengirim status pengurasan ke Firebase
void kirimStatusPengurasan(String status) {
  DateTime waktuSekarang = rtc.now();
  char stringWaktu[20];
  sprintf(stringWaktu, "%04d-%02d-%02d %02d:%02d:%02d",
          waktuSekarang.year(), waktuSekarang.month(), waktuSekarang.day(),
          waktuSekarang.hour(), waktuSekarang.minute(), waktuSekarang.second());

  HTTPClient http;
  DynamicJsonDocument dokumen(1024);
  dokumen["status"] = status;
  dokumen["nilaiKekeruhan"] = nilaiKekeruhan;
  dokumen["waktu"] = stringWaktu;

  String muatan;
  serializeJson(dokumen, muatan);

  // Tambahkan juga ke riwayat pengurasan
  http.begin(urlFirebase + "pengurasan/riwayat.json");
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(muatan);

  if (httpCode == HTTP_CODE_OK) {
    Serial.println("Riwayat pengurasan berhasil ditambahkan ke Firebase");
  } else {
    Serial.print("Gagal menambahkan riwayat pengurasan. Kode HTTP: ");
    Serial.println(httpCode);
  }
  http.end();
}

// Fungsi untuk mendapatkan level air kolam
float dapatkanLevelAir() {
  // Reset trigger pin
  digitalWrite(PIN_TRIGGER_KOLAM, LOW);
  delayMicroseconds(2);

  // Kirim pulsa 10 mikrodetik
  digitalWrite(PIN_TRIGGER_KOLAM, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIGGER_KOLAM, LOW);

  // Baca durasi pulsa gema
  long durasi = pulseIn(PIN_ECHO_KOLAM, HIGH);

  // Hitung jarak berdasarkan durasi
  float jarak = durasi * 0.034 / 2;  // cm

  // Tampilkan level air pada LCD
  lcd.setCursor(0, 1);
  lcd.print("Tinggi air: ");
  lcd.print(jarak);
  lcd.print(" cm");

  Serial.print("Tinggi air: ");
  Serial.print(jarak);
  Serial.println(" cm");

  return jarak;
}

// Fungsi mapping float (seperti map() versi float)
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Fungsi untuk mengecek nilai kekeruhan
int cekNilaiKekeruhan() {
  // Baca nilai ADC dari channel A0 pada ADS1115
  int16_t nilaiMentah = ads.readADC_SingleEnded(0);

  // Konversi nilai ADC menjadi kekeruhan (0–100 NTU misalnya)
  float kekeruhan = mapFloat(nilaiMentah, 17900, 19000, 100, 0)  // Nilai ADC besar = air jernih
  kekeruhan = constrain(kekeruhan, 0, 100); // Pastikan dalam rentang 0-100

  int kekeruhanNTU = round(kekeruhan); // Bulatkan ke NTU

  Serial.print("Nilai Kekeruhan: ");
  Serial.println(kekeruhan);

  // Kirim data kekeruhan ke Firebase setiap 5 detik
  if (millis() - waktuUpdateFirebaseTerakhir > 5000) {
    kirimKekeruhanKeFirebase(kekeruhan);
  }

  return kekeruhanNTU;
}

// Fungsi untuk menghitung stok pakan yang tersisa
float hitungStokPakan() {
  // Baca jarak dari sensor ultrasonic
  long jarak = sensorPakan.read();  // Jarak dalam cm

  Serial.print("Jarak: ");
  Serial.print(jarak);
  Serial.println(" cm");

  // Hitung tinggi pakan berdasarkan jarak
  float tinggiPakan = TINGGI_WADAH_PAKAN - jarak;

  // Hitung stok pakan berdasarkan persentase tinggi
  int stok = (tinggiPakan / TINGGI_WADAH_PAKAN) * STOK_PAKAN_PENUH;

  // Pastikan stok tidak negatif
  if (stok < 0) {
    stok = 0;
  }

  // Kirim data stok pakan ke Firebase setiap 5 detik
  if (millis() - waktuUpdateFirebaseTerakhir > 5000) {
    kirimStokPakanKeFirebase(stok);
  }

  return stok;
}

// Fungsi untuk memberikan pakan
void berikanPakan() {
  // Cek apakah stok pakan mencukupi
  if (stokPakan >= beratPakanPerPemberian) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Pemberian: " + String(beratPakanPerPemberian) + " Gram");

    // Aktifkan relay pemberi pakan
    digitalWrite(RELAY_PAKAN, HIGH);

    // Simulasi pemberian pakan secara bertahap
    int jumlahPakan = 0;
    while (jumlahPakan < beratPakanPerPemberian) {
      jumlahPakan++;
      lcd.setCursor(0, 1);
      lcd.print("Proses: " + String(jumlahPakan) + " Gram");
      delay(1000);
    }

    // Matikan relay pemberi pakan
    digitalWrite(RELAY_PAKAN, LOW);

    // Tampilkan pesan sukses
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Pemberian " + String(jumlahPakan) + " Gram");
    lcd.setCursor(0, 1);
    lcd.print("Pakan Berhasil...");

    // Kirim status berhasil ke Firebase
    kirimRiwayatPakanKeFirebase("Berhasil");
  } else {
    // Stok pakan tidak mencukupi
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Stok pakan habis");

    // Kirim status gagal ke Firebase
    kirimRiwayatPakanKeFirebase("Gagal");

    delay(2000);
    lcd.clear();
  }
}

// Fungsi untuk mengirim riwayat pemberian pakan ke Firebase
void kirimRiwayatPakanKeFirebase(String status) {
  DateTime waktuSekarang = rtc.now();
  char stringWaktu[20];
  sprintf(stringWaktu, "%04d-%02d-%02d %02d:%02d:%02d",
          waktuSekarang.year(), waktuSekarang.month(), waktuSekarang.day(),
          waktuSekarang.hour(), waktuSekarang.minute(), waktuSekarang.second());

  HTTPClient http;
  DynamicJsonDocument dokumen(1024);
  dokumen["status"] = status;
  dokumen["beratPakan"] = beratPakanPerPemberian;
  dokumen["waktu"] = stringWaktu;

  String muatan;
  serializeJson(dokumen, muatan);

  http.begin(urlFirebase + "pakan/riwayat.json");
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(muatan);

  if (httpCode == HTTP_CODE_OK) {
    Serial.println("Riwayat pemberian pakan berhasil dikirim ke Firebase");
  } else {
    Serial.print("Gagal mengirim riwayat pemberian pakan. Kode HTTP: ");
    Serial.println(httpCode);
  }
  http.end();
}

// Fungsi untuk mengambil jadwal pakan dan berat pakan dari Firebase
void ambilJadwalPakan() {
  HTTPClient http;
  String respons;
  DynamicJsonDocument dokumen(1024);

  // Ambil data jadwal
  http.begin(urlFirebase + "jadwal.json");
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    respons = http.getString();
    DeserializationError kesalahan = deserializeJson(dokumen, respons);

    if (kesalahan) {
      Serial.println("Gagal mengurai JSON untuk jadwal");
      return;
    }

    jumlahJadwal = dokumen.size();

    // Dealokasi array lama jika ada
    if (jadwalPemberian) delete[] jadwalPemberian;
    if (statusPemberianPakan) delete[] statusPemberianPakan;

    // Alokasi array baru
    jadwalPemberian = new Jadwal[jumlahJadwal];
    statusPemberianPakan = new bool[jumlahJadwal]();  // Inisialisasi semua nilai menjadi false

    // Isi array dengan data dari Firebase
    int indeks = 0;
    for (JsonPair pasangan : dokumen.as<JsonObject>()) {
      String waktu = pasangan.value().as<String>();
      jadwalPemberian[indeks].jam = waktu.substring(0, 2).toInt();
      jadwalPemberian[indeks].menit = waktu.substring(3, 5).toInt();
      indeks++;
    }
    Serial.println("Jadwal berhasil diperbarui");

    // Simpan jadwal ke penyimpanan non-volatile
    preferensi.begin("ikanpakan", false);

    // Cek dan simpan jumlah jadwal jika berubah
    int jumlahTersimpan = preferensi.getInt("jumlahJadwal", -1);
    if (jumlahTersimpan != jumlahJadwal) {
      preferensi.putInt("jumlahJadwal", jumlahJadwal);
    }

    // Cek dan simpan setiap jadwal hanya jika ada perubahan
    for (int i = 0; i < jumlahJadwal; i++) {
      String keyJam = "jam" + String(i);
      String keyMenit = "menit" + String(i);

      int jamTersimpan = preferensi.getInt(keyJam.c_str(), -1);
      int menitTersimpan = preferensi.getInt(keyMenit.c_str(), -1);

      if (jamTersimpan != jadwalPemberian[i].jam) {
        preferensi.putInt(keyJam.c_str(), jadwalPemberian[i].jam);
      }

      if (menitTersimpan != jadwalPemberian[i].menit) {
        preferensi.putInt(keyMenit.c_str(), jadwalPemberian[i].menit);
      }
    }

    preferensi.end();
  } else {
    Serial.println("Gagal mengambil jadwal pakan dari Firebase");
  }

  // Ambil berat pakan per pemberian
  http.begin(urlFirebase + "pakan/beratPakan.json");
  httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    respons = http.getString();
    DeserializationError kesalahan = deserializeJson(dokumen, respons);

    if (kesalahan) {
      Serial.println("Gagal mengurai JSON untuk berat pakan");
      return;
    }

    beratPakanPerPemberian = dokumen.as<int>();
    Serial.print("Berat Pakan Per Pemberian: ");
    Serial.println(beratPakanPerPemberian);

    // Simpan berat pakan ke penyimpanan non-volatile
    preferensi.begin("ikanpakan", false);

    // Ambil nilai yang tersimpan sebelumnya
    int beratTersimpan = preferensi.getInt("beratPakan", -1);

    // Simpan hanya jika nilai berubah
    if (beratTersimpan != beratPakanPerPemberian) {
      preferensi.putInt("beratPakan", beratPakanPerPemberian);
    }

    preferensi.end();
  } else {
    Serial.println("Gagal mengambil berat pakan dari Firebase");
  }

  http.end();
}

// Fungsi untuk mengirim stok pakan ke Firebase
void kirimStokPakanKeFirebase(float stokPakan) {
  DateTime waktuSekarang = rtc.now();
  char stringWaktu[20];
  sprintf(stringWaktu, "%04d-%02d-%02d %02d:%02d:%02d",
          waktuSekarang.year(), waktuSekarang.month(), waktuSekarang.day(),
          waktuSekarang.hour(), waktuSekarang.minute(), waktuSekarang.second());

  HTTPClient http;
  DynamicJsonDocument dokumen(1024);
  dokumen["stokPakan"] = stokPakan;
  dokumen["terakhirDiperbarui"] = stringWaktu;

  String muatan;
  serializeJson(dokumen, muatan);

  http.begin(urlFirebase + "sensor.json");
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.PATCH(muatan);

  if (httpCode == HTTP_CODE_OK) {
    Serial.println("Stok pakan berhasil dikirim ke Firebase");
  } else {
    Serial.print("Gagal mengirim stok pakan. Kode HTTP: ");
    Serial.println(httpCode);
  }
  http.end();
}

// Fungsi untuk mengirim nilai kekeruhan ke Firebase
void kirimKekeruhanKeFirebase(int kekeruhan) {
  HTTPClient http;
  DynamicJsonDocument dokumen(1024);
  dokumen["nilaiKekeruhan"] = kekeruhan;

  String muatan;
  serializeJson(dokumen, muatan);

  http.begin(urlFirebase + "sensor.json");
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.PATCH(muatan);

  if (httpCode == HTTP_CODE_OK) {
    Serial.println("Nilai kekeruhan berhasil dikirim ke Firebase");
  } else {
    Serial.print("Gagal mengirim nilai kekeruhan. Kode HTTP: ");
    Serial.println(httpCode);
  }
  http.end();
}

// Fungsi untuk memperbarui waktu RTC dari server NTP
void updateRTCdariNTP() {
  timeClient.begin();

  // Coba beberapa kali untuk mendapatkan waktu yang akurat
  int percobaan = 0;
  while (!timeClient.update() && percobaan < 5) {
    timeClient.forceUpdate();
    percobaan++;
    delay(500);
  }

  if (percobaan >= 5) {
    Serial.println("Gagal memperbarui waktu dari NTP");
    return;
  }

  // Dapatkan waktu epoch dari server NTP
  unsigned long waktuEpoch = timeClient.getEpochTime();
  waktuEpoch += 25200;  // Tambahkan 7 jam (25200 detik) untuk GMT+7

  // Buat objek DateTime dari waktu epoch
  DateTime waktuNTP(waktuEpoch);

  // Perbarui RTC dengan waktu dari NTP
  rtc.adjust(waktuNTP);
  Serial.println("RTC berhasil diperbarui dari NTP!");
}
