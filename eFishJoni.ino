#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <RTClib.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Ultrasonic.h>
#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 ads;  // Inisialisasi ADS1115

// Konfigurasi WiFi
const char* ssid = "BOLEH";
const char* password = "";
// // Konfigurasi WiFi
// const char* ssid = "Passwordnya cokicoki";
// const char* password = "cokicoki";

// Firebase URL
const String firebaseBaseURL = "https://fishfeeder-jonny-default-rtdb.asia-southeast1.firebasedatabase.app/";

// Konfigurasi RTC dan NTPRELAY_FEED
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

// Konfigurasi LCD
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Variabel waktu
DateTime now;

//variabel turbidity arduino uno
int turbidityValue = 0;

// Variabel dan Konstanta
const int minWaterLevel = 30;        // cm (batas air minimum)
const int maxWaterLevel = 20;        // cm (batas air maksimum)
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

  // Inisialisasi WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi connected");

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

  // Inisialisasi LCD
  lcd.init();
  lcd.backlight();

  // Inisialisasi LED dan Relay
  pinMode(LED_PIN, OUTPUT);
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

  // Ambil jadwal pakan dari Firebase
  ambilJadwalPakan();

  // Set waktu awal jika diperlukan (pastikan Anda hanya mengatur ini sekali)
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));  // Ini akan mengatur waktu ke waktu kompilasi
}

void loop() {
  // Cek status WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi tidak terhubung");
    lcd.setCursor(0, 3);
    lcd.print("WiFi tidak terhubung");
    return;
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

  delay(2000);

  lcd.setCursor(0, 0);
  lcd.print("Mulai pengisian..."); 

  digitalWrite(RELAY_FILL, HIGH);
  while (getWaterLevel() >= maxWaterLevel) {
    delay(1000);
  }
  digitalWrite(RELAY_FILL, LOW);
  Serial.println("Pengisian selesai.");
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
    delay(beratPakanSekali * waktuPerGram);
    digitalWrite(RELAY_FEED, LOW);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Stok pakan habis");
    delay(2000);
    lcd.clear();
  }
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
    lcd.setCursor(0, 3);
    lcd.print("Online");
  } else {
    lcd.setCursor(0, 3);
    lcd.print("Offline");
    Serial.println("Gagal mengambil jadwal dari Firebase");
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
  } else {
    Serial.println("Gagal mengambil berat pakan dari Firebase");
  }

  http.end();
}

// Fungsi untuk mengirim stok pakan ke Firebase
void kirimStokPakanKeFirebase(float stokPakan) {
  HTTPClient http;
  DynamicJsonDocument doc(1024);
  doc["stok"] = stokPakan;
  String payload;
  serializeJson(doc, payload);

  http.begin(firebaseBaseURL + "pakan.json");
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

  http.begin(firebaseBaseURL + "pakan.json");
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
