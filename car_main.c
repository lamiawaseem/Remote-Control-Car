//car
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "string.h"
#include "nvs_flash.h"
#include "esp_now.h"

#define TRIG_PIN GPIO_NUM_17
#define ECHO_PIN GPIO_NUM_16

#define LED1_GPIO 25
#define LED2_GPIO 26

#define IN1 18//back left
#define IN2 19
#define IN3 22//front left
#define IN4 21
#define IN5 32//back right
#define IN6 33
#define IN7 27//front right
#define IN8 14

static const char* TAG = "ESP-NOW BOTH";

// controller MAC
uint8_t peer_mac[6] = {0x68, 0xFE, 0x71, 0x90, 0x6C, 0x68};

int led = 0;
int x = 2048;//0-4095 is adc readings
int y = 2048;//sets x and y to centre so car doesnt immediately start moving

void motor_init();
void forward();
void backward();
void left();
void right();
void stop();
void brake();
void joystick();
void ultrasonic();

void send(const esp_now_send_info_t *info, esp_now_send_status_t status){
    //debug that checks if esp now is working, prints in terminal
    ESP_LOGI(TAG, "Send status: %s", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void recv(const esp_now_recv_info_t *info, const uint8_t *data, int len){
    //car recieves 2 types of data, joystick and headlight
    //joystick data was set as an array of ints so it could be differentiated from the led integer
    if (len == sizeof(int) * 2) {
        memcpy(&x, data, sizeof(int));//puts value into integers
        memcpy(&y, data + sizeof(int), sizeof(int));

        ESP_LOGI(TAG, "Joystick: x = %d y = %d", x, y);//debug
    }

    if (len == 1) {
        led = data[0];//puts value in int
        ESP_LOGI(TAG, "LED state received: %d", led);//debug
    }
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

void app_main() {
    //initalize gpios for headlight and ultra
    gpio_set_direction(TRIG_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);

    gpio_set_direction(LED1_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED2_GPIO, GPIO_MODE_OUTPUT);

    wifi_init();//start
    ESP_ERROR_CHECK(esp_now_init());//initialize

    esp_now_register_send_cb(send);//callback
    esp_now_register_recv_cb(recv);

    esp_now_peer_info_t peer = {0};//setup for esp-now, where to send
    memcpy(peer.peer_addr, peer_mac, 6);
    peer.channel = 1;
    esp_now_add_peer(&peer);

    //setup motor gpios
    motor_init();

    while (1) {
        ultrasonic();

        //my logic slightly backward(started on instead of off)
        // so i just inverted it instead of changing controller code(starts off now)
        gpio_set_level(LED1_GPIO, !led);
        gpio_set_level(LED2_GPIO, !led);

        joystick();

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void ultrasonic(){
    gpio_set_level(TRIG_PIN, 0);//trig starts low
    esp_rom_delay_us(2);
    gpio_set_level(TRIG_PIN, 1);//send 10us pulse
    esp_rom_delay_us(10);
    gpio_set_level(TRIG_PIN, 0);

    //wait for echo to start before continuing
    while (gpio_get_level(ECHO_PIN) == 0);
    int64_t start = esp_timer_get_time();

    // Wait for echo to be finished before continuing
    while (gpio_get_level(ECHO_PIN) == 1);
    int64_t end = esp_timer_get_time();

    //d=vt
    float distance = ((end - start) * 0.034) / 2;

    printf("Distance: %.2f cm\n", distance);//debug
    esp_now_send(peer_mac, (uint8_t *)&distance, sizeof(distance));
}

void joystick(){
    //2048 is centre value
    //+1000 or -1000 from that reading triggers direction response
    if(y > 3048){
        forward();
    }else if(y < 1048){
        backward();
    }else if(x > 3048){
        right();
    }else if(x < 1048){
        left();
    }else{
        stop();
    }
}

void motor_init(){
    //initialize all motor driver pins
    gpio_set_direction(IN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(IN2, GPIO_MODE_OUTPUT);
    gpio_set_direction(IN3, GPIO_MODE_OUTPUT);
    gpio_set_direction(IN4, GPIO_MODE_OUTPUT);
    gpio_set_direction(IN5, GPIO_MODE_OUTPUT);
    gpio_set_direction(IN6, GPIO_MODE_OUTPUT);
    gpio_set_direction(IN7, GPIO_MODE_OUTPUT);
    gpio_set_direction(IN8, GPIO_MODE_OUTPUT);
}

void forward(){
    gpio_set_level(IN1, 1);//back left fwd
    gpio_set_level(IN2, 0);
    gpio_set_level(IN3, 1);//front left fwd
    gpio_set_level(IN4, 0);
    gpio_set_level(IN5, 1);//back right fwd
    gpio_set_level(IN6, 0);
    gpio_set_level(IN7, 1);//front right fwd
    gpio_set_level(IN8, 0);
}

void backward(){
    gpio_set_level(IN1, 0);//back left bwd
    gpio_set_level(IN2, 1);
    gpio_set_level(IN3, 0);//front left bwd
    gpio_set_level(IN4, 1);
    gpio_set_level(IN5, 0);//back right bwd
    gpio_set_level(IN6, 1);
    gpio_set_level(IN7, 0);//front right bwd
    gpio_set_level(IN8, 1);
}

void right(){
    gpio_set_level(IN1, 0);//back left bwd
    gpio_set_level(IN2, 1);
    gpio_set_level(IN3, 0);//front left bwd
    gpio_set_level(IN4, 1);
    gpio_set_level(IN5, 1);//back right fwd
    gpio_set_level(IN6, 0);
    gpio_set_level(IN7, 1);//front right fwd
    gpio_set_level(IN8, 0);
}

void left(){
    gpio_set_level(IN1, 1);//back left fwd
    gpio_set_level(IN2, 0);
    gpio_set_level(IN3, 1);//front left fwd
    gpio_set_level(IN4, 0);
    gpio_set_level(IN5, 0);//back right bwd
    gpio_set_level(IN6, 1);
    gpio_set_level(IN7, 0);//front right bwd
    gpio_set_level(IN8, 1);
}

void stop(){
    gpio_set_level(IN1, 0);//back left stop
    gpio_set_level(IN2, 0);
    gpio_set_level(IN3, 0);//front left stop
    gpio_set_level(IN4, 0);
    gpio_set_level(IN5, 0);//back right stop
    gpio_set_level(IN6, 0);
    gpio_set_level(IN7, 0);//front right stop
    gpio_set_level(IN8, 0);
}