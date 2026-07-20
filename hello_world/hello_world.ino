void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ; // 시리얼 포트가 열릴 때까지 대기 (USB 네이티브 보드용)
  }
}

void loop() {
  Serial.println("Hello world");
  delay(1000);
}
