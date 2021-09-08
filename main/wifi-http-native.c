#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "driver/uart.h"

/*-----------------------------------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------------------------------*/
#define BLINK_GPIO 2//将gpio2定义为灯的io口
#define EXAMPLE_ESP_WIFI_SSID      "workGN-2.4"//要连接的wifi名称
#define EXAMPLE_ESP_WIFI_PASS      "work12345678"//要连接的wifi密码
#define EXAMPLE_ESP_MAXIMUM_RETRY   10//WIFI从新连接最多10次
#define WIFI_CONNECTED_BIT BIT0//连接标志位
#define WIFI_FAIL_BIT      BIT1//连接错误标志位
#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048
#define ECHO_TEST_TXD  (GPIO_NUM_17)//串口发送IO口
#define ECHO_TEST_RXD  (GPIO_NUM_16)//串口接收IO口
#define ECHO_TEST_RTS  (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS  (UART_PIN_NO_CHANGE)
#define EX_UART_NUM UART_NUM_1//串口号
#define BUF_SIZE (1024) 
char UART1_RX_data[9];
char getput_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};   // HTTP服务器返回的信息
char post_data [200]={0};//发向服务器的信息
char *timer_value="";//从HTTP服务器获取的时间
int  leak_position=0;//泄漏的位置
char UART1_TX_data[]={0x01,0x03,0x00,0x00,0x00,0x02,0xC4,0x0B};

/*-----------------------------------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------------------------------*/
void app_wifi_connect_sta_init(void);//wifi的sta配置初始化
//void app_uart_init(void);
/*-----------------------------------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------------------------------*/
static EventGroupHandle_t my_wifi_sta_event_group;//创建的wifi事件组名称
static const char *TAG = "my wifi station http post";//打印的头
static const char *TAG1 = "Leak Position";//打印的头
static int s_retry_num = 0;//wifi从新连接计数
/*------------------------------------wifi事件处理-----------------------------------------------------------------------*/
/*------------------------------------wifi事件处理-----------------------------------------------------------------------*/
/*------------------------------------wifi事件处理-----------------------------------------------------------------------*/
//WIFI连接事件
static void event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        esp_wifi_connect();
        ESP_LOGI(TAG, "my wifi connect");
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) 
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } 
        else 
        {
            xEventGroupSetBits(my_wifi_sta_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ipv4:" IPSTR, IP2STR(&event->ip_info.ip));
        ip_event_got_ip6_t* event1 =(ip_event_got_ip6_t* )event_data;
        ESP_LOGI(TAG, "got ipv6:" IPV6STR, IPV62STR(event1->ip6_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(my_wifi_sta_event_group, WIFI_CONNECTED_BIT);
    }
}

//wifi连接初始化函数
void app_wifi_connect_sta_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());//tcp/ip 初始化 
    my_wifi_sta_event_group = xEventGroupCreate(); //创建wifi事件组
    ESP_ERROR_CHECK(esp_event_loop_create_default());//创建默认循环事件
    esp_netif_create_default_wifi_sta();// 创建具有TCP/ip堆栈的默认网络接口实例绑定基站

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();//创建wifi默认配置结构体函数
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));//初始化wifi

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    //向默认循环注册WIFI_EVENT处理程序实例,如果发生获取了WIFI_EVENT（ESP_EVENT_ANY_ID），则执行回调函数event_handler，无额外参数传递
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,//要为其注册处理程序的事件的基本id
                                                        ESP_EVENT_ANY_ID,//要为其注册处理程序的事件的id
                                                        &event_handler,//当事件被分派时被调用的处理函数
                                                        NULL,//除事件数据外，在调用处理程序时传递给该处理程序的数据
                                                        &instance_any_id//与注册事件处理程序和数据相关的事件处理程序实例对象可以是NULL。
                                                        )
                                                        );
    //向默认循环注册IP_EVENT处理程序实例,如果发生获取了IP事件（IP_EVENT_STA_GOT_IP），则执行回调函数event_handler，无额外参数传递                                                    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    //wifi在模式sta下的配置
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
        },
    };    

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );//设置wifi为sta模式
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );//设置sta配置（将配置好的sta内容使能）
    ESP_ERROR_CHECK(esp_wifi_start() ); //wifi模式开始运行                                           
    
    ESP_LOGI(TAG, "wifi_init_sta finished，wifi的sta配置完成.");//打印设置完成
     
    EventBits_t bits = xEventGroupWaitBits(my_wifi_sta_event_group,//正在测试位的事件组
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,//事件组中要等待的标志位
                                           pdFALSE,//pdTRUE的意思是标志位返回前要清零，pdFALSE意思是返回前不需要清零
                                           pdFALSE,//pdTRUE所有标志位置位才可以，pdFALSE不是所有的标志位都等，任何一个置位就可以。
                                           portMAX_DELAY//等待时间
                                           ); 
    if (bits & WIFI_CONNECTED_BIT) 
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } 
    else if (bits & WIFI_FAIL_BIT) 
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } 
    else 
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }        
    

    

}
/*-------------------------------------HTTP任务相关函数----------------------------------------------------------------------*/
/*-------------------------------------HTTP任务相关函数----------------------------------------------------------------------*/
/*-------------------------------------HTTP任务相关函数----------------------------------------------------------------------*/
//_http_event_handler事件函数
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
                output_len = 0;
            }
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                if (output_buffer != NULL)
                {
                    free(output_buffer);
                    output_buffer = NULL;
                    output_len = 0;
                }
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;
}
//http_nation_request函数
static void http_native_request(void)
{
    
    int content_length = 0;
    esp_http_client_config_t config = {
        .url = "http://api.ubibot.cn/update.json?execute=true&metadata=true&api_key=92f08cc8527911cba869a2f1bbac54ad",
        .event_handler = _http_event_handler,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // GET Request
    esp_http_client_set_method(client, HTTP_METHOD_GET);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } 
    else 
    {
        content_length = esp_http_client_fetch_headers(client);
        if (content_length < 0) 
        {
            ESP_LOGE(TAG, "HTTP client fetch headers failed");
        } 
        else 
        {
            int data_read = esp_http_client_read_response(client, getput_buffer, MAX_HTTP_OUTPUT_BUFFER);
            if (data_read >= 0) 
            {
                ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
                ESP_LOG_BUFFER_HEX(TAG1,UART1_TX_data,sizeof(UART1_TX_data));
                ESP_LOG_BUFFER_HEX(TAG1,UART1_RX_data,sizeof(UART1_RX_data));
                //ESP_LOGI(TAG, "%s", getput_buffer);//打印字符串，注意双引号
                // ESP_LOG_BUFFER_HEX(TAG, getput_buffer, strlen(getput_buffer));
            } 
            else 
            {
                ESP_LOGE(TAG, "Failed to read response");
            }
        }
    }
    esp_http_client_close(client);

    cJSON *root = cJSON_Parse(getput_buffer);   //解析返回的时间json数据
    if(root!=NULL)
    {
        cJSON *time = cJSON_GetObjectItem(root,"server_time");
        timer_value = cJSON_GetStringValue(time);
        leak_position=(UART1_RX_data[5]*256+UART1_RX_data[6]);//计算uart1发送过来的数据
        snprintf(post_data,sizeof(post_data),"{\"feeds\":[{\"created_at\":\"%s\",\"field8\":%d}]}",timer_value,leak_position);//将获取的服务器时间和需要发送到服务器的数据复制给发送数组
        
        if(timer_value==NULL)
        {
            ESP_LOGI(TAG, "time error");
        }
        else
        {

        ESP_LOGI(TAG, "time = %s",timer_value);
       
        } 
    
    }
    cJSON_Delete(root);
    // POST Request
    esp_http_client_set_url(client, "http://api.ubibot.cn/update.json?execute=true&metadata=true&api_key=92f08cc8527911cba869a2f1bbac54ad");
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    err = esp_http_client_open(client, strlen(post_data));
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
    } 
    else 
    {
         int wlen = esp_http_client_write(client, post_data, strlen(post_data));//这个函数将把数据写入由esp_http_client_open()打开的HTTP连接。
        if (wlen < 0) 
        {
            ESP_LOGE(TAG, "Write failed");
        }
        content_length = esp_http_client_fetch_headers(client);//等待http返回应答数据，接收到数据就退出
        if (content_length < 0) 
        {
            ESP_LOGE(TAG, "HTTP client fetch headers failed");
        } 
        else 
        {
            int data_read = esp_http_client_read_response(client, getput_buffer, MAX_HTTP_OUTPUT_BUFFER);//读取服务端发来的数据，内部用了esp_http_client_read（）；函数
            if (data_read >= 0) 
            {
                ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d",
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
                ESP_LOGI(TAG, "%s",post_data);//打印字符串，注意双引号 
                ESP_LOGI(TAG, "%s", getput_buffer);//打印字符串，注意双引号 
                //ESP_LOG_BUFFER_CHAR(TAG, getput_buffer, strlen(getput_buffer));//打印字符，这个多好用啊
            } 
            else 
            {
                ESP_LOGE(TAG, "Failed to read response");
            }
        }
    }
    
    esp_http_client_cleanup(client);
 
}
//HTTP任务 
static void http_test_task(void *pvParameters)
{
    xEventGroupWaitBits(my_wifi_sta_event_group,//正在测试位的事件组
                                           WIFI_CONNECTED_BIT,//事件组中要等待的标志位
                                           pdFALSE,//pdTRUE的意思是标志位返回前要清零，pdFALSE意思是返回前不需要清零
                                           pdTRUE,//pdTRUE所有标志位置位才可以，pdFALSE不是所有的标志位都等，任何一个置位就可以。
                                           portMAX_DELAY//等待时间
                                           ); 
    ESP_LOGI(TAG, "Connected to AP, begin http example");
    while (1)
    {
       http_native_request();
       vTaskDelay(10000 / portTICK_PERIOD_MS);

        /* code */
    }
    ESP_LOGI(TAG, "Finish http example");
    vTaskDelete(NULL);
}
/*--------------------------------------灯bilnk任务---------------------------------------------------------------------*/
/*--------------------------------------灯bilnk任务---------------------------------------------------------------------*/
/*--------------------------------------灯bilnk任务---------------------------------------------------------------------*/
//灯bilnk任务
void task_blink(void *pvPar)
{
    
 	while(1)
	{
      if ((UART1_RX_data[0]==0x01)&&(UART1_RX_data[1]==0x03))
      {
        if ((UART1_RX_data[2]==0x04)&&(UART1_RX_data[4]==0x00))
        {
            gpio_set_level(BLINK_GPIO, 0);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            gpio_set_level(BLINK_GPIO, 1);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            
        }  
     
     
         if ((UART1_RX_data[2]==0x04)&&(UART1_RX_data[4]==0x01))
        {
            gpio_set_level(BLINK_GPIO, 0);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            gpio_set_level(BLINK_GPIO, 1);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            
        } 
      }
      
      
	}
    vTaskDelete(NULL);
} 
/*---------------------------------------uart任务--------------------------------------------------------------------*/
/*---------------------------------------uart任务--------------------------------------------------------------------*/
/*---------------------------------------uatr任务--------------------------------------------------------------------*/
 void app_uart_task(void *pvParameters)
{  int y=0;
     while (1) 
    {
        for(y=0;y<10;y++)
        {
            uart_write_bytes(EX_UART_NUM,UART1_TX_data,8);
        }
        uart_read_bytes(EX_UART_NUM, (uint8_t*)UART1_RX_data,9, 20 / portTICK_RATE_MS);//串口接收API
        vTaskDelay(1000 / portTICK_PERIOD_MS);   
    }
    vTaskDelete(NULL);
} 
/*---------------------------------------uart初始化--------------------------------------------------------------------*/
/*---------------------------------------uart初始化--------------------------------------------------------------------*/
/*---------------------------------------uatr初始化--------------------------------------------------------------------*/
void app_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = 4800,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(EX_UART_NUM, &uart_config);
    uart_set_pin(EX_UART_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS);

   
}
/*-----------------------------------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------------------------------*/
/*-----------------------------------------------------------------------------------------------------------*/
void app_main(void)
{
    gpio_pad_select_gpio(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
    app_wifi_connect_sta_init();//wifi连接初始化函数
    app_uart_init();
	//以下创建了灯blink的任务，使用的任务函数是task()
    xTaskCreatePinnedToCore(task_blink,           //任务函数
    		               "task",         //这个参数没有什么作用，仅作为任务的描述
			                2048,            //任务栈的大小
			                NULL,         //传给任务函数的参数
			                1,              //优先级，数字越大优先级越高
			               NULL,            //传回的句柄，不需要的话可以为NULL
			                0); //tskNO_AFFINITY在两个cup上运行，0表示在 Protocol CPU上，1表示在 Application CPU 
    
    xTaskCreate(http_test_task, "http_test_task", 8192, NULL, 5, NULL);
    xTaskCreate(app_uart_task, "uart_task", 1024, NULL, 1, NULL);   
}



