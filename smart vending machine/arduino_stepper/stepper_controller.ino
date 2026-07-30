#include <Stepper.h>
#include <SoftwareSerial.h>

#define STEPS_PER_REV 2048
#define DISPENSE_STEPS 3072

Stepper motor1(STEPS_PER_REV, 2, 4, 3, 5);
Stepper motor2(STEPS_PER_REV, 6, 8, 7, 9);
Stepper motor3(STEPS_PER_REV, 10, 12, 11, 13);

// Use pins 10 (RX) and 11 (TX) for Arduino <-> ESP32 serial communication
SoftwareSerial espSerial(10, 11);

const int dropSensorPin = A0;
const unsigned long DROP_TIMEOUT_MS = 3000;

void releaseMotor(byte a, byte b, byte c, byte d)
{
    digitalWrite(a, LOW);
    digitalWrite(b, LOW);
    digitalWrite(c, LOW);
    digitalWrite(d, LOW);
}

bool waitForDrop()
{
    unsigned long start = millis();
    while (millis() - start < DROP_TIMEOUT_MS)
    {
        if (digitalRead(dropSensorPin) == LOW)
        {
            return true;
        }
        delay(20);
    }
    return false;
}

void dispenseMotor(Stepper &motor, byte a, byte b, byte c, byte d)
{
    motor.step(DISPENSE_STEPS);
    bool dropped = waitForDrop();
    releaseMotor(a, b, c, d);

    if (dropped)
    {
        espSerial.println("DONE");
    }
    else
    {
        espSerial.println("FAILED");
    }
}

void setup()
{
    Serial.begin(9600);
    espSerial.begin(9600);

    pinMode(dropSensorPin, INPUT_PULLUP);
    motor1.setSpeed(12);
    motor2.setSpeed(12);
    motor3.setSpeed(12);

    releaseMotor(2, 3, 4, 5);
    releaseMotor(6, 7, 8, 9);
    releaseMotor(10, 11, 12, 13);
    Serial.println("Stepper controller ready");
}

void loop()
{
    if (espSerial.available())
    {
        String cmd = espSerial.readStringUntil('\n');
        cmd.trim();

        if (cmd == "M1")
        {
            dispenseMotor(motor1, 2, 4, 3, 5);
        }
        else if (cmd == "M2")
        {
            dispenseMotor(motor2, 6, 8, 7, 9);
        }
        else if (cmd == "M3")
        {
            dispenseMotor(motor3, 10, 12, 11, 13);
        }
    }
}
