#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <RTClib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Ultrasonic.h>
#include <Adafruit_ADS1X15.h>
#include <WiFiManager.h>  // Library WiFiManager
#include <Preferences.h>


Adafruit_ADS1115 ads;  // Inisialisasi ADS1115

// Konfigurasi WiFi
const char* ssid = "BOLEH";
const char* password = "";
// // Konfigurasi WiFi
// const char* ssid = "Passwordnya cokicoki";
// const char* password = "cokicoki";

// Firebase URL
const String firebaseBaseURL = "https://fishfeeder-jonny-default-rtdb.asia-southeast1.firebasedatabase.app/";

// Konfigurasi RTC dan NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "id.pool.ntp.org");  // GMT+7
RTC_DS3231 rtc;

// Konfigurasi Ultrasonic Pakan
#define TRIG_PIN1 33
#define ECHO_PIN1 32
#define TRIG_PIN2 26
#define ECHO_PIN2 25
Ultrasonic ultrasonic(TRIG_PIN1, ECHO_PIN1);

// Konfigurasi Relay dan LED
#define RELAY_FEED 14
#define RELAY_DRAIN 12
#define RELAY_FILL 13
#define LED_PIN 2
#define RESET_PIN 15

// Konfigurasi LCD
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Variabel waktu
DateTime now;
//Variabel Wifi Manager
WiFiManager wm;
//Variabel prefereneces
Preferences preferences;

//variabel turbidity arduino uno
int turbidityValue = 0;

// Variabel dan Konstanta
const int minWaterLevel = 30;       // cm (batas air minimum)
const int maxWaterLevel = 20;       // cm (batas air maksimum)
const int turbidityThreshold = 30;  // Sesuaikan dengan nilai sensor

const float tinggiWadah = 35.0;              // cm
const int stokPakanPenuh = 5000;             // gram
int stokPakan = 0;                           // gram
int beratPakanSekali = 0;                    // gram
int waktuPerGram = 1000;                     // ms per gram
unsigned long lastUpdateTime = 0;            // Waktu terakhir kali jadwal diperbarui
const unsigned long updateInterval = 60000;  // Interval untuk memeriksa update (60000 ms = 1 menit)

unsigned long lastFirebaseUpdate = 0;  // Waktu terakhir mengambil data dari Firebase

// Struktur untuk menyimpan jadwal pakan
struct Jadwal {
  int jam;
  int menit;
};

// Array dinamis untuk menyimpan jadwal pakan
Jadwal* jadwalPakan = nullptr;
int jumlahJadwal = 0;
bool* sudahMemberiPakan = nullptr;  // Status pemberian pakan

void setup() {
  Serial.begin(115200);
  Wire.begin();

  // Inisialisasi LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();

  //Inisialisasi LED_PIN
  pinMode(LED_PIN, OUTPUT);

  // Tampilkan pesan awal
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi...");

  // Menyalakan LED saat tombol reset ditekan
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
    digitalWrite(LED_PIN, HIGH);
  }

  // Coba koneksi otomatis
  if (!wm.autoConnect("Jfish", "12345678")) {
    Serial.println("Gagal terhubung ke WiFi, masuk ke mode AP...");
  }

  // Pastikan ESP tidak melanjutkan jika belum terhubung
  while (!WiFi.isConnected()) {
    delay(100);  // Tunggu hingga koneksi berhasil
  }

  Serial.println("WiFi connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Inisialisasi RTC
  if (!rtc.begin()) {
    Serial.println("RTC tidak terdeteksi");
    while (1)
      ;
  }

  // Cek apakah ADS1115 berhasil diinisialisasi
  if (!ads.begin()) {
    Serial.println("Gagal menghubungkan ke ADS1115. Periksa koneksi!");
    while (1)
      ;
  }

  // Inisialisasi LED dan Relay
  digitalWrite(LED_PIN, LOW);
  pinMode(RELAY_FEED, OUTPUT);
  digitalWrite(RELAY_FEED, LOW);
  pinMode(RELAY_FILL, OUTPUT);
  digitalWrite(RELAY_FILL, LOW);
  pinMode(RELAY_DRAIN, OUTPUT);
  digitalWrite(RELAY_DRAIN, LOW);

  // Inisialisasi Ultrasonic feed
  pinMode(TRIG_PIN1, OUTPUT);
  pinMode(ECHO_PIN1, INPUT);

  // Inisialisasi Ultrasonic Pool
  pinMode(TRIG_PIN2, OUTPUT);
  pinMode(ECHO_PIN2, INPUT);

  // Inisialisasi Pin RESET
  pinMode(RESET_PIN, INPUT_PULLUP);

  // Ambil jadwal pakan dari Firebase
  ambilJadwalPakan();

  // Coba baca data dari NVS
  preferences.begin("fishfeeder", false);
  jumlahJadwal = preferences.getInt("jumlahJadwal", 0);
  if (jumlahJadwal > 0) {
    jadwalPakan = new Jadwal[jumlahJadwal];
    sudahMemberiPakan = new bool[jumlahJadwal]();
    for (int i = 0; i < jumlahJadwal; i++) {
      jadwalPakan[i].jam = preferences.getInt(("jam" + String(i)).c_str(), 0);
      jadwalPakan[i].menit = preferences.getInt(("menit" + String(i)).c_str(), 0);
    }
    beratPakanSekali = preferences.getInt("beratPakan", 0);
    Serial.println("Data jadwal dan pakan dibaca dari NVS");
  } else {
    Serial.println("Tidak ada data jadwal dan pakan di NVS");
  }
  preferences.end();

  // Set waktu awal jika diperlukan (pastikan Anda hanya mengatur ini sekali)
  updateRTCFromNTP();
}

void loop() {
  // Cek status WiFi
  if (digitalRead(RESET_PIN) == LOW || WiFi.status() != WL_CONNECTED) {
    modeAP();
  }

  // Mendapatkan waktu dari RTC
  now = rtc.now();

  // Hitung stok pakan berdasarkan tinggi pakan
  stokPakan = cekStokPakan();
  turbidityValue = cekTurbidityValue();

  // Ambil jadwal pakan dari Firebase setiap 1 menit
  if (millis() - lastFirebaseUpdate >= 60000) {
    ambilJadwalPakan();
    lastFirebaseUpdate = millis();
  }

  // Tampilkan waktu dan stok pakan di LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Waktu: ");
  lcd.print(now.hour());
  lcd.print(":");
  lcd.print(now.minute());
  lcd.print(":");
  lcd.print(now.second());
  lcd.setCursor(0, 1);
  lcd.print("Stok: ");
  lcd.print(stokPakan);
  lcd.print(" gram");
  lcd.setCursor(0, 2);
  lcd.print("Turbidity: ");
  lcd.print(turbidityValue);
  lcd.print(" NTU");

  // Tampilkan jadwal terdekat
  Jadwal jadwalTerdekat = cariJadwalTerdekat();
  if (jadwalTerdekat.jam != -1 && jadwalTerdekat.menit != -1) {
    lcd.setCursor(0, 3);
    lcd.print("Next: ");
    lcd.print(jadwalTerdekat.jam);
    lcd.print(":");
    if (jadwalTerdekat.menit < 10) {
      lcd.print("0");  // Tambahkan leading zero untuk menit < 10
    }
    lcd.print(jadwalTerdekat.menit);
  } else {
    lcd.setCursor(0, 3);
    lcd.print("Tidak ada jadwal");
  }

  if (turbidityValue > turbidityThreshold) { pengurasan(); }

  // Cek jika sudah waktunya untuk memperbarui jadwal
  if (millis() - lastUpdateTime > updateInterval) {
    lastUpdateTime = millis();  // Reset waktu terakhir update

    // Cek jadwal pemberian pakan
    for (int i = 0; i < jumlahJadwal; i++) {
      if (now.hour() == jadwalPakan[i].jam && now.minute() == jadwalPakan[i].menit) {
        if (!sudahMemberiPakan[i]) {
          beriPakan();
          sudahMemberiPakan[i] = true;
        }
      } else {
        sudahMemberiPakan[i] = false;  // Reset status jika waktu berlalu
      }
    }
  }

  delay(1000);  // Delay untuk menghindari pembacaan terlalu cepat
}

// Fungsi untuk mencari jadwal terdekat
Jadwal cariJadwalTerdekat() {
  Jadwal jadwalTerdekat = { -1, -1 };  // Inisialisasi dengan nilai default
  DateTime now = rtc.now();            // Waktu saat ini

  int selisihTerdekat = INT_MAX;  // Selisih waktu terdekat

  for (int i = 0; i < jumlahJadwal; i++) {
    int selisihMenit = (jadwalPakan[i].jam - now.hour()) * 60 + (jadwalPakan[i].menit - now.minute());

    // Jika jadwal sudah lewat hari ini, tambahkan 24 jam (1440 menit)
    if (selisihMenit < 0) {
      selisihMenit += 1440;
    }

    // Cari jadwal dengan selisih terkecil
    if (selisihMenit < selisihTerdekat) {
      selisihTerdekat = selisihMenit;
      jadwalTerdekat = jadwalPakan[i];
    }
  }

  return jadwalTerdekat;
}

void modeAP() {
  Serial.println("Masuk Ke Mode Access Point");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mode Wi-Fi : Jfish");
  lcd.setCursor(0, 1);
  lcd.print("Akses configurasi");
  lcd.setCursor(0, 2);
  lcd.print("192.168.4.1");

  // Menyalakan LED saat tombol reset ditekan
  for (int i = 0; i < 10; i++) {
    digitalWrite(2, HIGH);
    delay(200);
    digitalWrite(2, LOW);
    delay(200);
    digitalWrite(2, HIGH);
  }

  delay(1000);                                // Debounce
  wm.resetSettings();                         // Reset semua kredensial WiFi tersimpan
  wm.startConfigPortal("Jfish", "12345678");  // Portal Wi-Fi

  Serial.println("Konfigurasi selesai, restart ESP32.");
  ESP.restart();  // Restart setelah konfigurasi
}

void pengurasan() {
  Serial.println("Mulai pengurasan...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mulai pengurasan...");

  digitalWrite(RELAY_DRAIN, HIGH);
  while (getWaterLevel() <= minWaterLevel) {
    delay(1000);
  }
  digitalWrite(RELAY_DRAIN, LOW);
  Serial.println("Pengurasan selesai, mulai pengisian...");

  cekTurbidityValue();

  delay(2000);

  lcd.setCursor(0, 0);
  lcd.print("Mulai pengisian...");

  digitalWrite(RELAY_FILL, HIGH);
  while (getWaterLevel() >= maxWaterLevel) {
    delay(1000);
  }
  digitalWrite(RELAY_FILL, LOW);
  Serial.println("Pengisian selesai.");

  cekTurbidityValue();
}

float getWaterLevel() {
  digitalWrite(TRIG_PIN2, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN2, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN2, LOW);

  long duration = pulseIn(ECHO_PIN2, HIGH);
  float distance = duration * 0.034 / 2;  // cm

  lcd.setCursor(0, 1);
  lcd.print("Tinggi air: ");
  lcd.print(distance);
  lcd.print(" cm");

  Serial.print("Tinggi air: ");
  Serial.print(distance);
  Serial.println(" cm");
  return distance;
}

int cekTurbidityValue() {
  int16_t turbidityRaw = ads.readADC_SingleEnded(0);  // Baca nilai ADC dari channel A0
  float turbidity = map(turbidityRaw, 0, 23800, 100, 0);

  Serial.print("TurbidityValue: ");
  Serial.println(turbidity);

  // Kirim data stok pakan ke Firebase hanya jika nilai stok berubah
  if (millis() - lastFirebaseUpdate > 5000) {
    kirimTurbidityKeFirebase(turbidity);
  }

  return turbidity;
}

float cekStokPakan() {
  long jarak = ultrasonic.read();  // Jarak dalam cm
  Serial.print("jarak: ");
  Serial.print(jarak);
  Serial.println(" cm");
  float tinggiPakan = tinggiWadah - jarak;  // Tinggi pakan
  int stok = (tinggiPakan / tinggiWadah) * stokPakanPenuh;

  if (stok < 0) {
    stok = 0;
  }

  // Kirim data stok pakan ke Firebase hanya jika nilai stok berubah
  if (millis() - lastFirebaseUpdate > 5000) {
    kirimStokPakanKeFirebase(stok);
  }

  return stok;
}

void beriPakan() {
  if (stokPakan >= beratPakanSekali) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Makani: " + String(beratPakanSekali) + " Gram");
    digitalWrite(RELAY_FEED, HIGH);

    int jumlahPakan = 0;
    while (jumlahPakan < beratPakanSekali) {
      jumlahPakan++;
      lcd.setCursor(0, 1);
      lcd.print("Proses: " + String(jumlahPakan) + " Gram");
      delay(1000);
    }
    //delay(beratPakanSekali * waktuPerGram);
    digitalWrite(RELAY_FEED, LOW);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Pemberian " + String(jumlahPakan) + " Gram");
    lcd.setCursor(0, 1);
    lcd.print("Pakan Berahasil..");
    kirimHistoryPakanKeFirebase("Berhasil");
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Stok pakan habis");
    kirimHistoryPakanKeFirebase("Gagal");
    delay(2000);
    lcd.clear();
  }
}

// Fungsi untuk mengirim stok pakan ke Firebase
void kirimHistoryPakanKeFirebase(String status) {

  DateTime now = rtc.now();
  char timeString[20];
  sprintf(timeString, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

  HTTPClient http;
  DynamicJsonDocument doc(1024);
  doc["status"] = status;
  doc["beratPakan"] = beratPakanSekali;  // Tanpa konversi ke String
  doc["waktu"] = timeString;

  String payload;
  serializeJson(doc, payload);

  http.begin(firebaseBaseURL + "pakan/riwayat.json");
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(payload);

  if (httpCode == HTTP_CODE_OK) {
    Serial.println("History Pakan berhasil dikirim ke Firebase");
  } else {
    Serial.print("Gagal mengirim history pakan. HTTP Code: ");
    Serial.println(httpCode);
  }
  http.end();
}

// Fungsi untuk mengambil jadwal pakan dan berat pakan dari Firebase
void ambilJadwalPakan() {
  HTTPClient http;
  String payload;
  DynamicJsonDocument doc(1024);

  // Ambil data jadwal
  http.begin(firebaseBaseURL + "jadwal.json");
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    payload = http.getString();
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.println("Gagal parsing JSON");
      return;
    }
    jumlahJadwal = doc.size();
    if (jadwalPakan) delete[] jadwalPakan;
    if (sudahMemberiPakan) delete[] sudahMemberiPakan;

    jadwalPakan = new Jadwal[jumlahJadwal];
    sudahMemberiPakan = new bool[jumlahJadwal]();

    int idx = 0;
    for (JsonPair jadwal : doc.as<JsonObject>()) {
      String waktu = jadwal.value().as<String>();
      jadwalPakan[idx].jam = waktu.substring(0, 2).toInt();
      jadwalPakan[idx].menit = waktu.substring(3, 5).toInt();
      idx++;
    }
    Serial.println("Jadwal berhasil diperbarui");

    // Simpan jadwal ke NVS
    preferences.begin("fishfeeder", false);
    preferences.putInt("jumlahJadwal", jumlahJadwal);
    for (int i = 0; i < jumlahJadwal; i++) {
      preferences.putInt(("jam" + String(i)).c_str(), jadwalPakan[i].jam);
      preferences.putInt(("menit" + String(i)).c_str(), jadwalPakan[i].menit);
    }
    preferences.end();
  } else {
    Serial.println("Gagal mengambil jadwal pakan dari Firebase");
  }

  // Ambil berat pakan
  http.begin(firebaseBaseURL + "pakan/berat.json");
  httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    payload = http.getString();
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
      Serial.println("Gagal parsing JSON untuk berat pakan");
      return;
    }
    beratPakanSekali = doc.as<int>();
    Serial.print("Berat Pakan Sekali: ");
    Serial.println(beratPakanSekali);

    // Simpan berat pakan ke NVS
    preferences.begin("fishfeeder", false);
    preferences.putInt("beratPakan", beratPakanSekali);
    preferences.end();
  } else {
    Serial.println("Gagal mengambil berat pakan dari Firebase");
  }

  http.end();
}

// Fungsi untuk mengirim stok pakan ke Firebase
void kirimStokPakanKeFirebase(float stokPakan) {

  DateTime now = rtc.now();
  char timeString[20];
  sprintf(timeString, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

  HTTPClient http;
  DynamicJsonDocument doc(1024);
  doc["stok"] = stokPakan;
  doc["lastUpdate"] = timeString;
  String payload;
  serializeJson(doc, payload);

  http.begin(firebaseBaseURL + "sensor.json");
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.PATCH(payload);

  if (httpCode == HTTP_CODE_OK) {
    Serial.println("Stok Pakan berhasil dikirim ke Firebase");
  } else {
    Serial.print("Gagal mengirim stok pakan. HTTP Code: ");
    Serial.println(httpCode);
  }
  http.end();
}

// Fungsi untuk mengirim stok pakan ke Firebase
void kirimTurbidityKeFirebase(int turbidity) {
  HTTPClient http;
  DynamicJsonDocument doc(1024);
  doc["turbidity"] = turbidity;
  String payload;
  serializeJson(doc, payload);

  http.begin(firebaseBaseURL + "sensor.json");
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.PATCH(payload);

  if (httpCode == HTTP_CODE_OK) {
    Serial.println("Turbidity berhasil dikirim ke Firebase");
  } else {
    Serial.print("Gagal mengirim Turbidity. HTTP Code: ");
    Serial.println(httpCode);
  }
  http.end();
}

void updateRTCFromNTP() {
  timeClient.begin();

  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }

  unsigned long epochTime = timeClient.getEpochTime();
  epochTime += 25200;  // GMT+7

  DateTime ntpTime(epochTime);
  rtc.adjust(ntpTime);
  Serial.println("RTC updated from NTP!");
}
