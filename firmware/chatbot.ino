#include "SimpleRingBuffer.h"
#include "SparkIntervalTimer/SparkIntervalTimer.h"

#define MICROPHONE_PIN DAC1
#define AUDIO_BUFFER_MAX 8192
#define BUTTON_PIN D0
#define SPEAKER_PIN A3

#define SINGLE_PACKET_MIN 512
#define SINGLE_PACKET_MAX 1024
#define END_PACKET_SIZE 100

#define SERIAL_DEBUG_ON true
#define SERVER_HOST  "192.168.2.64"
#define SERVER_PORT 3000

#define AUDIO_TIMING_VAL 62 /* 16kHz */

uint8_t txBuffer[SINGLE_PACKET_MAX + 1];
SimpleRingBuffer audio_buffer;
SimpleRingBuffer recv_buffer;

unsigned long lastRead = micros();
unsigned long lastSend = millis();

TCPClient client;
IntervalTimer readMicTimer;

int _sendBufferLength = 0;
unsigned int lastPublished = 0;
bool _isRecording = false;
bool _isConnected = false;
float _volumeRatio = 1;
int lastClientCheck;


void setup() {
    #if SERIAL_DEBUG_ON
    Serial.begin(115200);
    #endif

    setADCSampleTime(ADC_SampleTime_3Cycles);
    pinMode(MICROPHONE_PIN, INPUT);
    pinMode(D7, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    int mySampleRate = AUDIO_TIMING_VAL;

    Particle.variable("sample_rate", &mySampleRate, INT);
    Particle.publish("sample_rate", " my sample rate is: " + String(AUDIO_TIMING_VAL));
    Particle.function("recognized", recognized);

    recv_buffer.init(AUDIO_BUFFER_MAX);
    audio_buffer.init(AUDIO_BUFFER_MAX);

    lastRead = micros();
}


void loop() {
    verifyConnected();

    // Record and send audio
    if (digitalRead(BUTTON_PIN) == LOW && _isConnected) {
        digitalWrite(D7, HIGH);
        startRecording();
        sendEvery(100);
    }  else {
        if (_isConnected) {
            readAndPlay();
        }
    
        digitalWrite(D7, LOW);
        stopRecording();
    }
}

void startRecording() {
    if (!_isRecording) {
        _isRecording = true;
        readMicTimer.begin(readMic, AUDIO_TIMING_VAL, uSec);
    }
}


void stopRecording() {
    if (_isRecording) {
        _isRecording = false;
        readMicTimer.end();
        sendEnd();
    }
}

void readMic(void) {
    //read audio
    uint16_t value = analogRead(MICROPHONE_PIN);
    value = map(value, 0, 4095, 0, 255);
    audio_buffer.put(value);
}

void sendEvery(int delay) {
    // if it's been longer than 100ms since our last broadcast, then broadcast.
    if ((millis() - lastSend) >= delay) {
        sendAudio();
        lastSend = millis();
    }
}

void sendAudio(void) {
    int count = 0;
    int storedSoundBytes = audio_buffer.getSize();

    // don't read out more than the max of our ring buffer
    // remember, we're also recording while we're sending
    while (count < storedSoundBytes) {

        if (audio_buffer.getSize() < SINGLE_PACKET_MIN) {
            break;
        }
        // for loop should be faster, since we can check our buffer size just once?
        int size = min(audio_buffer.getSize(), SINGLE_PACKET_MAX);

        for(int c = 0; c < size; c++) {
            txBuffer[c] = audio_buffer.get();
        }
        count += size;

        client.write(txBuffer, size);
    }
}

void sendEnd(void) {
    
    for(int c = 0; c < END_PACKET_SIZE; c++) {
        txBuffer[c] = NULL;
    }
    
    client.write(txBuffer, END_PACKET_SIZE);
}

void playRxAudio() {
    unsigned long lastWrite = micros();
	unsigned long now, diff;
	int value;

    while (recv_buffer.getSize() > 0) {

        //play audio
        value = recv_buffer.get();
        value = map(value, 0, 255, 0, 4095);
        value = value * _volumeRatio;

        now = micros();
        diff = (now - lastWrite);
        if (diff < AUDIO_TIMING_VAL) {
            delayMicroseconds(AUDIO_TIMING_VAL - diff);
        }

        analogWrite(SPEAKER_PIN, value);
        lastWrite = micros();
    }
}

void readAndPlay() {
    while (client.available()) {
      recv_buffer.put(client.read());
      Serial.print("message recieved");
    }

    playRxAudio();
}


void write_socket(TCPClient socket, uint8_t *buffer, int count) {
    socket.write(buffer, count);
}

int recognized(String text) {
    Serial.println("Recognized: " + text);

    return 0;
}

bool verifyConnected() {
    unsigned int now = millis();
    if ((now - lastClientCheck) > 100) {
        lastClientCheck = now;

        if (client.connected()) {
            _isConnected = true;
        }
        else {
            _isConnected = client.connect(SERVER_HOST, SERVER_PORT);
        }
    }
    
    return _isConnected;
}
