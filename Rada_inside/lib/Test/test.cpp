#include "test.h"

void test_mode()
{
    printf("Test start ......\r\n");
    bool user_button = false;
    for(int i = 0; i < 1000; i++)
    {
        delay(20);
        if(digitalRead(WAKE_PIN) == LOW)
        {
            digitalWrite(LED_PIN, HIGH);
            buzzer_on(30, 2);
            digitalWrite(WAKE_PIN, LOW);
            user_button = true;
            break;
        }
    }

    if(user_button)
    {
        printf("User button pressed ......\r\n");
    }
    else
    {
        printf("User button test failed ......\r\n");
    }

    esp_restart();
}


