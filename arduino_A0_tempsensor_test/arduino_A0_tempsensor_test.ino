/* Testar A0 och tempsensorn
Tom Johans 2025-2026 */

const int tempPin = A0;

void setup() {
  Serial.begin(115200);

  Serial.println();
  Serial.println("Termistortest startat");
  Serial.println("Tid(s)\tADC\tVolt");
}

void loop() {
  int raw = analogRead(tempPin);

  float voltage = raw * (5.0 / 1023.0);

  Serial.print(millis() / 1000);
  Serial.print("\t");

  Serial.print(raw);
  Serial.print("\t");

  Serial.println(voltage, 3);

  delay(5000);
}