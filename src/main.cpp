#include <Arduino.h>

// put function declarations here:
int myFunction(int, int);
int result = 0;

void setup() {
    Serial.begin(115200);
  // put your setup code here, to run once:
  result = myFunction(2, 3);

}

void loop() {
  // put your main code here, to run repeatedly:
  Serial.print("Resultado");
  Serial.println(result);
  delay(1000); // Aguarda 1 segundo entre cada envio
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}