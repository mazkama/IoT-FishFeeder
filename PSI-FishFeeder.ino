#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <RTClib.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Konfigurasi WiFi
const char* ssid = "rfvalensia_";
const char* password = "vlnsiaaa";

// Firebase Config
#define FIREBASE_URL "https://mobile-smt5-default-rtdb.asia-southeast1.firebasedatabase.app/device/10000000001/jadwal.json"

// Konfigurasi RTC dan NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "id.pool.ntp.org", 25260);  // GMT+7
RTC_DS3231 rtc;

// Konfigurasi Ultrasonic
#define TRIG_PIN 5
#define ECHO_PIN 18

// Konfigurasi LCD
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Konfigurasi Relay
#define RELAY_PIN 12
#define LED_PIN 2

// Variabel dan Konstanta
const float tinggiWadah = 20.0;              // cm
const int stokPakanPenuh = 2000;              // gram
int stokPakan = 0;                           // gram
int beratPakanSekali = 0;                    // gram
int waktuPerGram = 1000;                     // ms per gram
unsigned long lastUpdateTime = 0;            // Waktu terakhir kali jadwal diperbarui
const unsigned long updateInterval = 60000;  // Interval untuk memeriksa update (60000 ms = 1 menit)


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

  // Sinkronisasi waktu NTP
  timeClient.begin();
  timeClient.update();

  // Inisialisasi RTC
  if (!rtc.begin()) {
    Serial.println("RTC tidak terdeteksi");
    while (1)
      ;
  }

  // Inisialisasi LCD
  lcd.init();
  lcd.backlight();

  // Inisialisasi LED
  pinMode(LED_PIN, OUTPUT);

  // Inisialisasi Relay
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // Inisialisasi Ultrasonic
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Ambil jadwal pakan dari Firebase
  ambilJadwalPakan();
}

void loop() {
  timeClient.update();
  DateTime now = rtc.now();

  // Hitung stok pakan berdasarkan tinggi pakan
  stokPakan = cekStokPakan();
  ambilJadwalPakan();  // Ambil jadwal terbaru dari Firebase

  // Tampilkan waktu dan stok pakan di LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Waktu: ");
  lcd.print(now.hour());
  lcd.print(":");
  lcd.print(now.minute());
  lcd.setCursor(0, 1);
  lcd.print("Stok: ");
  lcd.print(stokPakan);
  lcd.print(" gram");

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

  delay(5000);  // Delay untuk menghindari pembacaan terlalu cepat
}

float cekStokPakan() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long durasi = pulseIn(ECHO_PIN, HIGH);
  float jarak = durasi * 0.034 / 2;         // Jarak dalam cm
  float tinggiPakan = tinggiWadah - jarak;  // Tinggi pakan
  int stok = (tinggiPakan / tinggiWadah) * stokPakanPenuh;

  if (stok < 0) {
    stok = 0;
  }
  kirimStokPakanKeFirebase(stok);

  return stok;
}

void beriPakan() {
  if (stokPakan >= beratPakanSekali) {
    sendHistoryPakan();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Makani : " + String(beratPakanSekali) + " Gram");
    pinMode(LED_PIN, HIGH);
    digitalWrite(RELAY_PIN, HIGH);
    delay(beratPakanSekali * waktuPerGram);
    digitalWrite(RELAY_PIN, LOW);
    pinMode(LED_PIN, LOW);
  } else {
    sendHistoryPakan();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Stok pakan habis");
    delay(2000);
    lcd.clear();
  }
}

// Fungsi untuk mengambil jadwal pakan dari Firebase
// Fungsi untuk mengambil jadwal pakan dan berat pakan dari Firebase
void ambilJadwalPakan() {
  // Buat objek HTTPClient untuk request ke Firebase
  HTTPClient http;

  // Ambil data jadwal
  http.begin(FIREBASE_URL);   // URL Firebase untuk jadwal pakan
  int httpCode = http.GET();  // Kirim GET request

  if (httpCode == HTTP_CODE_OK) {       // Jika request berhasil
    String payload = http.getString();  // Ambil data JSON

    // Gunakan ArduinoJson untuk parsing JSON
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.println("Gagal parsing JSON");
      return;
    }

    // Ambil data jadwal dan simpan ke dalam array
    jumlahJadwal = doc.size();
    jadwalPakan = new Jadwal[jumlahJadwal];
    sudahMemberiPakan = new bool[jumlahJadwal]();

    int idx = 0;
    // Iterasi atas objek JSON dan ambil nilai dari setiap kunci
    for (JsonPair jadwal : doc.as<JsonObject>()) {
      String waktu = jadwal.value().as<String>();  // Ambil nilai waktu dari JSON
      int jam = waktu.substring(0, 2).toInt();
      int menit = waktu.substring(3, 5).toInt();
      jadwalPakan[idx].jam = jam;
      jadwalPakan[idx].menit = menit;
      idx++;
    }

    Serial.println("Jadwal berhasil diperbarui"); 
    lcd.setCursor(0, 2);
    lcd.print("Online");
  } else { 
    lcd.setCursor(0, 2);
    lcd.print("Offline");
    Serial.println("Gagal mengambil jadwal dari Firebase");
  }

  // Ambil data berat pakan dari Firebase
  http.begin("https://mobile-smt5-default-rtdb.asia-southeast1.firebasedatabase.app/device/10000000001/pakan/berat.json");  // URL Firebase untuk berat pakan
  httpCode = http.GET();                                                                                                    // Kirim GET request

  if (httpCode == HTTP_CODE_OK) {       // Jika request berhasil
    String payload = http.getString();  // Ambil data JSON

    // Parsing JSON untuk mendapatkan berat pakan
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.println("Gagal parsing JSON untuk berat pakan");
      return;
    }

    beratPakanSekali = doc.as<int>();  // Ambil nilai berat pakan
    Serial.print("Berat Pakan Sekali: ");
    Serial.println(beratPakanSekali);
  } else {
    Serial.println("Gagal mengambil berat pakan dari Firebase");
  }

  // Tutup koneksi HTTP
  http.end();
}

// Fungsi untuk mengirim stok pakan ke Firebase
void kirimStokPakanKeFirebase(float stokPakan) {
  // Buat objek HTTPClient untuk request ke Firebase
  HTTPClient http;

  // Siapkan payload dalam format JSON
  DynamicJsonDocument doc(1024);
  doc["stok"] = stokPakan;

  String payload;
  serializeJson(doc, payload);  // Serialize JSON menjadi string

  // Kirim PUT request untuk memperbarui stok pakan
  http.begin("https://mobile-smt5-default-rtdb.asia-southeast1.firebasedatabase.app/device/10000000001/pakan.json");  // URL Firebase untuk stok pakan
  http.addHeader("Content-Type", "application/json");                                                                 // Set header untuk JSON

  int httpCode = http.PATCH(payload);  // Kirim data JSON ke Firebase

  if (httpCode == HTTP_CODE_OK) {  // Jika request berhasil
    Serial.println("Stok Pakan berhasil dikirim ke Firebase"); 
  } else { 
    Serial.print("Gagal mengirim stok pakan. HTTP Code: ");
    Serial.println(httpCode);
  }

  // Tutup koneksi HTTP
  http.end();
}

// Method Tunggal: Mengambil waktu RTC dan mengirimkan data ke server
void sendHistoryPakan() {
  const char* serverName = "https://pkl.supala.fun/api/historyPakan";
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    // Ambil waktu dari RTC
    DateTime now = rtc.now();
    char waktu[20];
    sprintf(waktu, "%04d-%02d-%02d %02d:%02d:%02d",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());

    // Data JSON
    String id_device = "10000000001";
    String jsonData = String("{\"id_device\":\"") + id_device + "\",\"waktu\":\"" + waktu + "\"}";

    // Kirim HTTP POST
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(jsonData);
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("Response:");
      Serial.println(response);
    } else {
      Serial.print("Error on sending POST: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("Wi-Fi Disconnected");
  }
}