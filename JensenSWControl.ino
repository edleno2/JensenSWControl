#include <Arduino.h>
#include <ArduinoOTA.h>
#include <driver/dac.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>

#include "teleplot.h"

// ADC2 is used by wifi - so use ADC1 for all ADC channels.
// DAC is on 17 and 18, but an internal pulldown on 18 causes problems so use 17

#define GPIO_LED 15
#define GPIO_KEY1_IN 9
#define GPIO_KEY2_IN 7
#define GPIO_TIP_OUT 17
#define GPIO_RING_OUT 33
#define GPIO_OUTPUT_ADC_SENSOR 5



enum KeyPressed
{
    None = 0,
    PhoneOn,
    PhoneOff,
    Pause,
    Stop,
    TrackNext,
    TrackPrev,
    VolUp,
    VolDown,
    Gps,
    Mute
};

int key1Min;
int key1Max;
int key2Min;
int key2Max;

long mutePressedCount = 0;
KeyPressed priorKeyPress = KeyPressed::None;

int  outValues [11] [2] = 
{
    HIGH, 255,      // none
    LOW, 41,        // phone on (600 mv)
    LOW, 83,        // phone off (1010 mv)
    LOW, 221,       // voice mapped to pause (2628 mv)
    HIGH, 221,      // band mapped to stop (2628 mv)
    HIGH, 132,      // track next (600 mv)
    HIGH, 155,      // track prev (1810 mv)
    HIGH, 165,      // vol up (2070 mv)
    HIGH, 190,      // vol down (2320 mv)
    HIGH, 40,       // menu mapped to GPS (594 mv)
    HIGH, 75        // mute (1000 mv)
};

// UDP for teleplot
WiFiUDP Udp;
Teleplot* teleplot = 0;

// WiFiManager
WiFiManager wm;


void setup()
{
    Serial.begin(115200);
    delay(3000);

    Serial.println("JensenSWControl has started");
    ResetMinMax();

    pinMode(GPIO_LED, GPIO_MODE_OUTPUT);
    pinMode(GPIO_RING_OUT, GPIO_MODE_OUTPUT);
    digitalWrite(GPIO_RING_OUT, HIGH);
    dac_output_enable(DAC_CHANNEL_1);
    dacWrite(GPIO_TIP_OUT, 0);               // now set it to min 

}

void loop()
{
    if (WiFi.isConnected())
    {
        ArduinoOTA.handle();
    }
    
    //Serial.println("checking the input");
    KeyPressed keyPress = CheckInput();
    if (keyPress != KeyPressed::None)
    {
        priorKeyPress = keyPress;
    }
    else // no key pressed - if a prior key was valid then process it (we process key input on key UP not down)
    {
        if (priorKeyPress != KeyPressed::None)
        {
            Serial.printf("Key pressed: %d \r\n", priorKeyPress);
            if (teleplot)
            {
                teleplot->log("Key was pressed");
                teleplot->update("KeyPress", (int)priorKeyPress, 10);
            }
       
            if (priorKeyPress == KeyPressed::Mute)
            {
                mutePressedCount++;
                if (mutePressedCount > 5)
                {
                    // blink the led with a long pause
                    digitalWrite(GPIO_LED, HIGH);
                    delay(1500);
                    digitalWrite(GPIO_LED, LOW);
 
                    Serial.println("Mute was pressed more than 5 times");
                    // try to start a wifi session using the wifi manager.  If a password is stored and valid
                    // the wifi will start.  If not, a Wifi AP will run for the configured time to allow user to 
                    // put in the wifi credentials
                    //wm.resetSettings();    // enable this to delete old settings
                    wm.setConnectTimeout(30);   // try to connect for 30 seconds then switch to the AP
                    wm.setConfigPortalTimeout(180);   // user has up to 3 minutes to put in credentials if needed
                    bool res = wm.autoConnect("JensenSWControl");
                    if (res == false)
                    {
                        Serial.println("Failed to connect to Wifi and/or establish an access point in time");
                    }

                    if (WiFi.isConnected())
                    {
                        Serial.println("Wifi is connected");
                        teleplot = new Teleplot("LENOIR-LENOVO:42769");
                        teleplot->log("JensenSWControl has enabled wifi");
                        ArduinoOTA.begin();
                    }
                    else
                    {
                        Serial.println("failed to start wifi");
                    }


                }
            }
            else
            {
                mutePressedCount = 0;
            }
        
            SendOutput(priorKeyPress);
            priorKeyPress = KeyPressed::None;

        }
    }

    
    
    delay(100);
}

KeyPressed CheckInput()
{
    int key1 = analogReadMilliVolts(GPIO_KEY1_IN);
    int key2 = analogReadMilliVolts(GPIO_KEY2_IN);
 
    //*** key is "high" when nothing pressed
    if (key1 > 2000 and key2 > 2000)
    {
        ResetMinMax();
        return KeyPressed::None;
    }

    if (teleplot)
    {
        teleplot->update("Key1ADC", key1, 10);
        teleplot->update("Key2ADC", key2, 10);
    }
    

    if (key1 < key1Min)
    {
        key1Min = key1;
    }
    if (key1 > key1Max)
    {
        key1Max = key1;
    }
    

    if (key2 < key2Min)
    {
        key2Min = key2;
    }
    if (key2 > key2Max)
    {
        key2Max = key2;
    }
    
 

    //Serial.printf("Key1 Min: %4d Max: %4d Key2 Min: %4d Max: %4d \r\n", key1Min, key1Max, key2Min, key2Max);

    if (key1 < 140)
    {
        return KeyPressed::PhoneOff;
    }
    
    if ( key1 < 205)
    {
        return KeyPressed::Pause;
    }

    if (key1 < 350)
    {
        return KeyPressed::VolUp;
    }

    if (key1 < 500)
    {
        return KeyPressed::Stop;
    }
    

    if (key1 < 650)
    {
        return KeyPressed::TrackPrev;
    }
    
    // check key2 values (all may be above max - error on key1 value)

    if (key2 < 75)
    {
        return KeyPressed::PhoneOn;
    }

    if (key2 < 875)
    {
        return KeyPressed::VolDown;
    }

    if (key2 < 1200)
    {
        return KeyPressed::Mute;
    }

    if (key2 < 1675)
    {
        return KeyPressed::TrackNext;
    }
    
    if (key2 < 1900)
    {
        return KeyPressed::Gps;
    }
    
    
    Serial.printf("Didn't match any ranges.  Key1 Min: %4d Max: %4d Key2 Min: %4d Max: %4d \r\n", key1Min, key1Max, key2Min, key2Max);
    

    // Serial.print("Taking default action for key press.  Key1: ");
    // Serial.print(key1);
    // Serial.print(" key2: ");
    // Serial.println(key2);

    return KeyPressed::None;
}

 void ResetMinMax()
 {
    key1Min = 9999;
    key1Max = 0;
    key2Min = 9999;
    key2Max = 0;
 }   

void SendOutput(int keyPress)
{
    int hiLo = outValues[keyPress][0];    // value is 0 or 1
    int value = outValues[keyPress][1];   // value is in units (0 to 255)
    
    digitalWrite(GPIO_RING_OUT, hiLo);
    dacWrite(GPIO_TIP_OUT, value);
    int adcOut = analogReadMilliVolts(GPIO_OUTPUT_ADC_SENSOR);
    Serial.printf("Sending output. Keypress: %d Ring Units: %d Requested: %d adc Measured: %d\r\n", keyPress, hiLo, value, adcOut);

    digitalWrite(GPIO_LED, HIGH);
    if (teleplot)
    {
        teleplot->log("sending output for key press");
        teleplot->update("keyPressOut", keyPress, 10);
        teleplot->update("RequestValue", value, 10);
        teleplot->update("adcOutput", adcOut, 10);
    }

    delay(200);

    digitalWrite(GPIO_RING_OUT, HIGH);
    dacWrite(GPIO_TIP_OUT, 0);
    digitalWrite(GPIO_LED, LOW);
    Serial.println("Output complete.");
}