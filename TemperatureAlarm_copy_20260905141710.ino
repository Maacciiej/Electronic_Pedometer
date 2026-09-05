#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MPU6050.h>
#include <math.h>
#include <Preferences.h>

Preferences prefs;
// I2C 
#define I2C_SDA 21
#define I2C_SCL 22

//  KONFIG WIFI 
// const char* ssid     = "Galaxy S20 FE 5G 19EB";
// const char* password = "ttqy5329";
const char* ssid     = "Mi 11 Lite 5G";
const char* password = "wuz5baenwfqmsk8";
// const char* ssid     = "PLAY_Swiatlowod_DC00";
// const char* password = "7C14fwFxj7@t";
//OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
unsigned long lastOLED = 0;

MPU6050 mpu;
WebServer server(80);


// ======= ZMIENNE KROKOMIERZA =======
volatile int krok_counter = 0;      // “surowe” (kroki wykryte algorytmem)
int wyswietlane_kroki = 0;          // mnożone ×2 dla prezentacji
float acc_filtered = 0;
bool step_in_progress = false;
unsigned long last_step_time = 0;
int16_t prev_az = 0;

// ======= HISTORIA (ostatnie 15 min; okno 10 s) =======
static const int WINDOW_SEC = 10;
static const int HISTORY_POINTS = 90; // 90*10s = 15 min
int window_steps = 0;
unsigned long last_window_tick = 0;
int history[HISTORY_POINTS];
int histHead = 0; // indeks do kołowego bufora

// ======= STRONA HTML (Chart.js z CDN) =======
const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width,initial-scale=1" />
<title>Krokomierz ESP32</title>
<link rel="icon" href="data:,">
<style>
  body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Inter,Arial,sans-serif;padding:16px;max-width:900px;margin:auto;background:#0b0f14;color:#e6edf3}
  .card{background:#111826;border:1px solid #243244;border-radius:14px;padding:16px;margin:12px 0;box-shadow:0 8px 24px rgba(0,0,0,.3)}
  h1{font-size:22px;margin:0 0 8px}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
  .big{font-size:42px;font-weight:700;letter-spacing:.5px}
  .muted{opacity:.7}
  button{background:#1f6feb;border:0;color:white;padding:10px 14px;border-radius:10px;font-weight:600;cursor:pointer}
  button:active{transform:translateY(1px)}
  canvas{width:100%;height:320px;background:#0c121a;border-radius:8px}
  #err{color:#ff9aa2;margin-top:8px;min-height:1.2em}
</style>
</head>
<body>
  <div class="card">
    <h1>Krokomierz ESP32</h1>
    <div class="grid">
      <div>
        <div class="muted">Łączna liczba kroków</div>
        <div id="total" class="big">—</div>
      </div>
      <div>
        <div class="muted">Tempo (kroki / min)</div>
        <div id="pace" class="big">—</div>
      </div>
    </div>
    <div style="margin-top:10px">
      <button onclick="resetSteps()">Zeruj licznik</button>
      <div id="err"></div>
    </div>
  </div>

  <div class="card">
    <h1>Ostatnie 15 minut</h1>
    <div class="muted" id="updated">Aktualizacja: —</div>
    <canvas id="hist" width="800" height="320"></canvas>
  </div>

<script>
let labels = [], data = [];

function drawChart(){
  const c = document.getElementById('hist');
  const ctx = c.getContext('2d');
  ctx.clearRect(0,0,c.width,c.height);

  const padL=40, padR=10, padT=10, padB=30;
  const w = c.width - padL - padR;
  const h = c.height - padT - padB;

  // ramka
  ctx.strokeStyle = '#243244';
  ctx.lineWidth = 1;
  ctx.strokeRect(padL, padT, w, h);

  const maxVal = Math.max(1, ...data);
  const barW = Math.max(2, Math.floor(w / Math.max(1,data.length)));

  // osie
  ctx.fillStyle = '#8aa4c2';
  ctx.font = '12px system-ui';
  ctx.fillText('0', 8, padT + h);
  ctx.fillText(String(maxVal), 8, padT + 10);

  // słupki
  for(let i=0;i<data.length;i++){
    const x = padL + i*barW;
    const bh = Math.round(h * (data[i] / maxVal));
    const y = padT + (h - bh);
    ctx.fillStyle = '#1f6feb';
    ctx.fillRect(x, y, Math.max(1, barW-1), bh);
  }
}

async function fetchData(){
  try{
    const r = await fetch('/data.json', {cache:'no-store'});
    if(!r.ok) throw new Error('HTTP '+r.status);
    const j = await r.json();

    document.getElementById('total').textContent = j.total_steps;
    document.getElementById('pace').textContent  = Math.round(j.pace_per_min);

    labels = j.labels || [];
    data   = j.history || [];
    drawChart();

    document.getElementById('updated').textContent = 'Aktualizacja: ' + new Date().toLocaleTimeString();
    document.getElementById('err').textContent = '';
  }catch(e){
    document.getElementById('err').textContent = 'Błąd pobierania: ' + (e.message||e);
  }
}

function resetSteps(){
  fetch('/reset', {method:'POST'}).then(()=>fetchData()).catch(()=>{});
}

setInterval(fetchData, 1000);
fetchData();
</script>
</body>
</html>)HTML";

// ======= HANDLERY HTTP =======
void handleIndex(){
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

String twoDigits(int v){ if(v<10) return "0"+String(v); return String(v); }

// Zwróć JSON z aktualnymi danymi
void handleData(){
  // policz tempo: suma punktów z ostatniej minuty
  int sumLastMin = 0;
  for(int i=0;i<6;i++){ // 6*10s = 60s
    int idx = (histHead - 1 - i + HISTORY_POINTS) % HISTORY_POINTS;
    sumLastMin += history[idx];
  }
  // labels HH:MM dla 90 punktów
  String labels="[";
  String hist="[";
  unsigned long now = millis();
  for(int i=HISTORY_POINTS-1;i>=0;i--){
    int idx = (histHead - 1 - i + HISTORY_POINTS) % HISTORY_POINTS;
    // “czas” orientacyjny – tylko do etykiet
    unsigned long msAgo = (unsigned long)i * WINDOW_SEC * 1000UL;
    unsigned long t = (now >= msAgo ? now - msAgo : 0);
    // nie mamy RTC – pokażemy tylko mm:ss wstecz
    unsigned long secs = t/1000UL;
    unsigned mm = (secs/60)%60;
    unsigned ss = secs%60;
    labels += "\"" + twoDigits(mm) + ":" + twoDigits(ss) + "\"";
    hist += String(history[idx]);
    if(i>0){ labels += ","; hist += ","; }
  }
  labels += "]";
  hist += "]";

  String json = "{";
  json += "\"total_steps\":" + String(krok_counter) + ",";
  json += "\"pace_per_min\":" + String(sumLastMin) + ",";
  json += "\"labels\":" + labels + ",";
  json += "\"history\":" + hist;
  json += "}";

int total_now = krok_counter;
Serial.print("HTTP /data.json -> total_now=");
Serial.print(total_now);
Serial.print(" (krok_counter=");
Serial.print(krok_counter);
Serial.println(")");

  server.send(200, "application/json", json);
}

void handleReset(){
  krok_counter = 0;
  wyswietlane_kroki = 0;
  server.send(200, "text/plain", "OK");
}

void handleRawCounter(){
  server.send(200, "text/plain", String(krok_counter));
}

// ======= POMOCNICZE =======
bool safeFinite(float v){ return isfinite(v); }

//OLED
void updateOLED() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE); // Upewnij się, że kolor jest ustawiony
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("STATUS: Polaczono"); 
  
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print("KROKI:");
  
  display.setCursor(0, 45);
  display.print(krok_counter); // Wyświetlaj przemnożone kroki, tak jak w BLE/WWW
  display.display();
}

// ======= SETUP =======
void setup(){
  Serial.begin(115200);
  // I2C  MPU
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  WiFi.setSleep(false); 
  delay(300);
  mpu.initialize();
  Serial.println("MPU zainicjalizowane");

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED nie wykryty");
    while (1) delay(100);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE); 
  display.display();


  prefs.begin("krokomierz", false);
  
// Odczyt zapisanych danych
krok_counter = prefs.getInt("kroki", 0);
wyswietlane_kroki = prefs.getInt("wysw", krok_counter);
histHead = prefs.getInt("histHead", 0);

// Odczyt historii
for(int i = 0; i < HISTORY_POINTS; i++){
  history[i] = prefs.getInt(("h" + String(i)).c_str(), 0);
}

wyswietlane_kroki = krok_counter;

Serial.println("Dane wczytane z FLASH");

  last_window_tick = millis() - WINDOW_SEC * 1000UL; 



  // inicjalizacja filtra z pierwszego odczytu
  int16_t ax,ay,az; mpu.getAcceleration(&ax,&ay,&az);
  acc_filtered = sqrt((double)ax*ax + (double)ay*ay + (double)az*az);
  prev_az = az;

  // Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Łączenie z Wi-Fi");
  unsigned long t0 = millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t0<20000){
    delay(500); Serial.print(".");
  }
  if(WiFi.status()==WL_CONNECTED){
    Serial.print("\n IP: "); Serial.println(WiFi.localIP());
  }else{
    Serial.println("\n Brak połączenia z Wi-Fi (timeout). Spróbuj ponownie.");
  }

  server.on("/counter", handleRawCounter);

  // HTTP
  server.on("/", handleIndex);
  server.on("/data.json", handleData);
  server.on("/reset", HTTP_POST, handleReset);
  server.begin();
}

// ======= PĘTLA =======
void loop(){
  server.handleClient();

  // odczut MPU oraz ALGORYTM KROKU 
  int16_t ax,ay,az; mpu.getAcceleration(&ax,&ay,&az);

  double axd=ax, ayd=ay, azd=az; 
  float acc_total = sqrt(axd*axd + ayd*ayd + azd*azd);

  if(!safeFinite(acc_total) || acc_total<100 || acc_total>50000){
    // pomijamy nieprawidłowy pomiar
  }else{

    // filtr responsywny
    acc_filtered = 0.8f*acc_filtered + 0.2f*acc_total;

    int16_t acc_diff_z = abs(az - prev_az);
    prev_az = az;

    unsigned long t = millis(); 

    bool warunek_kroku = (acc_filtered > 17200.0f) && (acc_diff_z > 1000); 
    bool odstep = (t - last_step_time > 400);

    // log co 2s
    static unsigned long last_log=0;
    if(t-last_log>2000){
      Serial.print("T="); Serial.print(t);
      Serial.print(" | acc_filt="); Serial.print(acc_filtered);
      Serial.print(" | az="); Serial.print(az);
      Serial.print(" | diff_z="); Serial.print(acc_diff_z);
      Serial.print(" | warunek_kroku="); Serial.print(warunek_kroku);
      Serial.print(" | wykryty="); Serial.print(step_in_progress);
      Serial.print(" | odstęp_OK="); Serial.print(odstep);
      Serial.println();
      last_log=t;
    }

    if(warunek_kroku && !step_in_progress && odstep){
    krok_counter++;
    step_in_progress = true;
    last_step_time = t;

    // 🔥 NATYCHMIASTOWY ZAPIS KROKÓW
    prefs.putInt("kroki", krok_counter);

    Serial.println("✅ KROK (zapis)");
}

//OLED
if (millis() - lastOLED > 300) {
    lastOLED = millis();
    updateOLED();
  }
    if(acc_filtered < 16400.0f || (t - last_step_time > 1500)){
      step_in_progress = false;
    }
  }

  // === OKNA CZASOWE / HISTORIA ===
unsigned long now = millis();
if(now - last_window_tick >= WINDOW_SEC*1000UL){
  int newSteps = (krok_counter) - wyswietlane_kroki;
  if(newSteps < 0) newSteps = 0;

  history[histHead] = newSteps;
  histHead = (histHead + 1) % HISTORY_POINTS;

  wyswietlane_kroki = krok_counter;
  last_window_tick = now;

  //  Archiwizacja
  prefs.putInt("kroki", krok_counter);
  prefs.putInt("wysw", wyswietlane_kroki);
  prefs.putInt("histHead", histHead);

  for(int i = 0; i < HISTORY_POINTS; i++){
    prefs.putInt(("h" + String(i)).c_str(), history[i]);
  }

  Serial.println("Dane zapisane do FLASH");
}

  delay(50); // ~20 Hz próbkowania
}