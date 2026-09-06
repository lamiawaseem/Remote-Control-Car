//controller
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include <math.h>
#include "esp_wifi.h"
#include "esp_log.h"
#include "string.h"
#include "nvs_flash.h"
#include "esp_now.h"
#include "esp_adc/adc_oneshot.h"

#define LCD_BUTTON_GPIO 22
#define LED_BUTTON_GPIO 23
#define X_GPIO ADC_CHANNEL_6 //34
#define Y_GPIO ADC_CHANNEL_7 //35

// D0-D7, RS, EN
int lcd_pins[10] = {13, 14, 27, 26, 25, 33, 32, 5, 17, 4};
int x = 0;
int y = 0;

char msg[32] = "holder";//character array that will hold distance data from car to print

adc_oneshot_unit_handle_t adc1_handle;//adc setup
static const char* TAG = "ESP-NOW BOTH";

// car MAC
uint8_t peer_mac[6] = {0x20, 0xE7, 0xC8, 0xED, 0x05, 0x0C};

void lcd_init();
void lcd_cms(unsigned char);
void lcd_data(unsigned char);
void lcd_decode(unsigned char);
void lcd_string(unsigned char*);

void send(const esp_now_send_info_t *info, esp_now_send_status_t status){
    //debug that checks if esp now is working, prints in terminal
    ESP_LOGI(TAG, "Send status: %s", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void recv(const esp_now_recv_info_t *info, const uint8_t *data, int len){
    //distance recieved from ultrasonic on car
    float distance;
    memcpy(&distance, data, sizeof(float));//puts value into float

    snprintf(msg, sizeof(msg), "%.2fcm", distance);//puts distance value into string msg for lcd
    ESP_LOGI(TAG, "Received: %s", msg);//debug
}

void wifi_init(){
    nvs_flash_init();//initlaizes flash storage needed for wifi
    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();//default wifi setup
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();//start
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);//sets esp32 to channel 1, both esp must have same
}

void app_main(void){
    //Enables internal pull-up resistor so its set to 1 till buttons pressed
    gpio_set_direction(LCD_BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(LCD_BUTTON_GPIO, GPIO_PULLUP_ONLY);

    gpio_set_direction(LED_BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(LED_BUTTON_GPIO, GPIO_PULLUP_ONLY);

    int lcd_state = 0;//tracks if lcd on or off
    int lcd_old_state = 1;//previous state

    int led_state = 0;
    int led_old_state = 1;

    wifi_init();//start
    ESP_ERROR_CHECK(esp_now_init());//initialize

    esp_now_register_send_cb(send);//callback
    esp_now_register_recv_cb(recv);

    esp_now_peer_info_t peer = {0};//setup for esp-now, where to send
    memcpy(peer.peer_addr, peer_mac, 6);
    peer.channel = 1;
    esp_now_add_peer(&peer);

    //configure adc, channel setup
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&init_config, &adc1_handle);

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,//0-4095V readings
    };

    //setup x and y pins
    adc_oneshot_config_channel(adc1_handle, X_GPIO, &config);
    adc_oneshot_config_channel(adc1_handle, Y_GPIO, &config);

    //continuous loop begins here
    while(1){
        //reads current state
        int lcd_button = gpio_get_level(LCD_BUTTON_GPIO);
        int led_button = gpio_get_level(LED_BUTTON_GPIO);

        //read x and y pins
        adc_oneshot_read(adc1_handle, X_GPIO, &x);
        adc_oneshot_read(adc1_handle, Y_GPIO, &y);

        //readings stored in array
        int joystick[2] = {x, y};
        esp_now_send(peer_mac, (uint8_t*)joystick, sizeof(joystick));

        //debug
        printf("joystick x: %d  y: %d\n", x, y);

        //determines if button is pressed, not held
        if(lcd_old_state == 1 && lcd_button == 0){
            //changes state, if on turns off, vis verca
            lcd_state = !lcd_state;

            if (lcd_state) {
                lcd_init();
            } else {
                lcd_cms(0x08);
            }
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
        lcd_old_state = lcd_button;//updates old state

        //same logic as lcd button above
        if(led_old_state == 1 && led_button == 0){
            led_state = !led_state;

            uint8_t data = led_state;
            esp_now_send(peer_mac, &data, sizeof(data));

            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
        led_old_state = led_button;

        if (lcd_state) {
            lcd_cms(0x01);
            lcd_cms(0x80);
            lcd_string((unsigned char*)msg);
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void lcd_init(){
    //initalizes pins by selecting them in the array one by one and setting them to output
    for(int i = 0; i < 10; i++){
        esp_rom_gpio_pad_select_gpio(lcd_pins[i]);
        gpio_set_direction(lcd_pins[i], GPIO_MODE_OUTPUT);
    }
   
    lcd_cms(0x38);//configure lcd in 80bit mode
    lcd_cms(0x01);//clears the display
    lcd_cms(0x0E);//turns on screen and displays cursor
    lcd_cms(0x80);//sets the cursor to first row, first column
    lcd_string((unsigned char*)msg);//prints string to lcd
}

void lcd_decode(unsigned char info){
    //sends 1 byte of data to each pin, parallel signals
    unsigned char temp;

    for(int i = 0; i < 8; i++){
        temp = pow(2, i);
        gpio_set_level(lcd_pins[i], (info & temp));
    }
}

void lcd_cms(unsigned char cmd){
    lcd_decode(cmd);

    gpio_set_level(lcd_pins[8], 0);//rs set to 0, command mode
    gpio_set_level(lcd_pins[9], 1);//en set to 1

    vTaskDelay(10/portTICK_PERIOD_MS);//10milisecond delay
    gpio_set_level(lcd_pins[9], 0);//en set to 0, lcd reads on falling edge
    vTaskDelay(10/portTICK_PERIOD_MS);
}

void lcd_data(unsigned char data){
    lcd_decode(data);

    gpio_set_level(lcd_pins[8], 1);//rs set to 1, character mode
    gpio_set_level(lcd_pins[9], 1);//en set to 1

    vTaskDelay(10/portTICK_PERIOD_MS);
    gpio_set_level(lcd_pins[9], 0);//en set to 0
    vTaskDelay(10/portTICK_PERIOD_MS);
}

void lcd_string(unsigned char *p){
    //goes through string and sends data by each character to lcd
    while(*p != '\0'){
        lcd_data(*p);
        p++;
    }
}