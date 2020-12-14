#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/semphr.h"
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "freertos/event_groups.h"

#include "freertos/queue.h"
#include "driver/touch_pad.h"
#include "soc/rtc_periph.h"
#include "soc/sens_periph.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sleep.h"
#include "esp32/ulp.h"

#include "driver/adc.h"
#include "driver/rtc_io.h"

#include "soc/rtc.h"

EventGroupHandle_t estadoComputador;

#define pcLock (1<<0)

typedef struct node {
    float val;
    struct node *next;
} node_t;

int queueSize(node_t *head)
{
    node_t *current = head;
    int size = 0;

    while (current != NULL)
    {
        current = current->next;
        size++;
    }
    return size;
}

void enqueue(node_t **head, float val) {
    node_t *new_node = malloc(sizeof(node_t));
    if (!new_node) return;

    new_node->val = val;
    new_node->next = *head;

    *head = new_node;
}

float dequeue(node_t **head) {
    node_t *current, *prev = NULL;
    float retval = -1;

    if (*head == NULL) return -1;

    current = *head;
    while (current->next != NULL) {
        prev = current;
        current = current->next;
    }

    retval = current->val;
    free(current);

    if (prev)
        prev->next = NULL;
    else
        *head = NULL;

    return retval;
}

void print_list(node_t *head) {
    node_t *current = head;

    while (current != NULL) {
        printf("%.2f\n", current->val);
        current = current->next;
    }
}

#define MAXPOSVET 15

TaskHandle_t esteiraUmHandle;
TaskHandle_t esteiraDoisHandle;
TaskHandle_t esteiraTresHandle;
TaskHandle_t DisplayShowHandle;

/// Variáveis Globais
float pesoVet[1500];
uint32_t cont = 0;
uint32_t nTotalProdutos = 0;

float pesoTotal = 0.0;
float pesoAnterior = 0.0;

SemaphoreHandle_t mutual_exclusion_mutex;
uint32_t soma = 0;

void ComputadorCentral(float peso)
{
    if(cont<MAXPOSVET)
    {
        pesoVet[cont] = peso;
        printf("Valor inserido no vetor[%d]: %.2f\n",cont,pesoVet[cont]);
        nTotalProdutos++;
        cont++;
    }
    if(cont==MAXPOSVET)
    {
        printf("\nAcabou %d \ntotal de produtos %d \n\n",cont, nTotalProdutos);
        //uint64_t eus, eus2;
        //eus = esp_timer_get_time();
        vTaskSuspendAll();

        for (int i = 0; i < MAXPOSVET; i++)
        {
            pesoTotal+=pesoVet[i];
        }
        
        pesoAnterior = pesoTotal;
        //printf("\nPeso Total %.2f \n\n\n", pesoTotal);
        cont = 0;

        for(int i = 0; i<1500; i++)
        {
            pesoVet[i]=0.0;
        }

        if(!xTaskResumeAll())
        {
            taskYIELD();
        }
        //eus2 = esp_timer_get_time();
        //for(int i  = 0; i<MAXPOSVET; i++)
        //{
        //    printf("VET[%d]: %.2f\n",i,pesoVet[i]);
        //}
        printf("\nPeso Total %.2f \n\n\n", pesoTotal);
    }
    
    xEventGroupClearBits(estadoComputador, BIT0);//sinaliza pc livre
}

void EsteiraTres(void *pvParameter)
{
    float peso = 0.5, pesoAux = 0;
    node_t *filaTres = NULL;
    //const char *my_name = "\nEsteira 1";

    while(1)
    {
        //uint64_t eus, eus2;
        //eus = esp_timer_get_time();
        vTaskDelay(100 / portTICK_RATE_MS);
        
        enqueue(&filaTres, peso);
        //if pc livre entra //usar algum timpo de msg buffer ou msg event
        ESP_LOGI("Est. 3","Funcionando");
        if(!xEventGroupWaitBits(estadoComputador, pcLock, true, true, pdMS_TO_TICKS(0)))
        {
            xSemaphoreTake(mutual_exclusion_mutex, portMAX_DELAY);
            xEventGroupSetBits(estadoComputador, pcLock);
            if(cont+queueSize(filaTres)<=MAXPOSVET)
            {
                for(int k = 0; (pesoAux=dequeue(&filaTres))>0; k++)
                {
                    ComputadorCentral(pesoAux);
                }
            }
            else if(cont<MAXPOSVET)
            {
                ComputadorCentral(dequeue(&filaTres));
            }
            xSemaphoreGive(mutual_exclusion_mutex);
        }
        //eus2 = esp_timer_get_time();
        //printf("\nTempo: %llu\n - Value of Tick in 1 ms: %u\n", (eus2 - eus), portTICK_PERIOD_MS);
    }
}

void EsteiraDois(void *pvParameter)
{
    float peso = 2.0, pesoAux = 0;
    node_t *filaDois = NULL;
    //const char *my_name = "\nEsteira 1";

    while(1)
    {
        vTaskDelay(500 / portTICK_RATE_MS);
        enqueue(&filaDois, peso);

        ESP_LOGI("Est. 2","Funcionando");
        if(!xEventGroupWaitBits(estadoComputador, pcLock, true, true, pdMS_TO_TICKS(0)))//if pc livre entra //usar algum timpo de msg buffer ou msg event
        {
            xSemaphoreTake(mutual_exclusion_mutex, portMAX_DELAY);
            xEventGroupSetBits(estadoComputador, pcLock);
            if(cont+queueSize(filaDois)<=MAXPOSVET)
            {
                //printf("\n %.2f",peso);
                for(int k = 0; (pesoAux=dequeue(&filaDois))>0; k++)
                {
                    ComputadorCentral(pesoAux);
                }
            }
            else if(cont<MAXPOSVET)
            {
                ComputadorCentral(dequeue(&filaDois));
            }
            xSemaphoreGive(mutual_exclusion_mutex);
        }
    }
}

void EsteiraUm(void *pvParameter)
{
    float peso = 5.0, pesoAux = 0;
    node_t *filaUm = NULL;

    while(1)
    {
        //uint64_t eus, eus2;
        //eus = esp_timer_get_time();
        vTaskDelay(1000 / portTICK_RATE_MS);

        enqueue(&filaUm, peso);

        ESP_LOGI("Est. 1","Funcionando");
        if(!xEventGroupWaitBits(estadoComputador, pcLock, true, true, pdMS_TO_TICKS(0)))
        {
            xEventGroupSetBits(estadoComputador, pcLock);
            xSemaphoreTake(mutual_exclusion_mutex, portMAX_DELAY);

            if(cont+queueSize(filaUm)<=MAXPOSVET)
            {
                for(int k = 0; (pesoAux=dequeue(&filaUm))>0; k++)
                {
                    ComputadorCentral(pesoAux);
                }
            }
            else if(cont<MAXPOSVET)
            {
                ComputadorCentral(dequeue(&filaUm));
            }
            xSemaphoreGive(mutual_exclusion_mutex);
            //vTaskSuspend(NULL);
        }        
        //eus2 = esp_timer_get_time();
        //printf("\nTempo: %llu\n - Value of Tick in 1 ms: %u\n", (eus2 - eus), portTICK_PERIOD_MS);
    }
}

void DisplayShow(void *pvParameter)
{
    //uint64_t eus, eus2;
    while(1)
    {
        //eus = esp_timer_get_time();
        vTaskDelay(2000 / portTICK_PERIOD_MS);

        printf("\nTotal de Itens: %d Peso Total: %.2f\n", nTotalProdutos, pesoAnterior);
        printf("\nTotal de Itens: %d Peso Total: %.2f\n", nTotalProdutos, pesoAnterior);
        printf("\nTotal de Itens: %d Peso Total: %.2f\n", nTotalProdutos, pesoAnterior);
        //eus2 = esp_timer_get_time();
        //printf("\nTempo: %llu\n - Value of Tick in 1 ms: %u\n", (eus2 - eus), portTICK_PERIOD_MS);
    }
}

void app_main()
{
    nvs_flash_init();
    mutual_exclusion_mutex = xSemaphoreCreateMutex();
    if(mutual_exclusion_mutex != NULL)
    {
        printf("Mutex was created\n");
    }

    estadoComputador = xEventGroupCreate();

    for(uint32_t i = 0; i<MAXPOSVET;i++)
    {
        pesoVet[i]=0.0;
    }

    xTaskCreatePinnedToCore(&EsteiraUm,   "EsteiraUm",   2048, NULL, 2, &esteiraUmHandle, 1);
    xTaskCreatePinnedToCore(&EsteiraDois, "EsteiraDois", 2048, NULL, 2, &esteiraDoisHandle, 1);
    xTaskCreatePinnedToCore(&EsteiraTres, "EsteiraTres", 2048, NULL, 2, &esteiraTresHandle, 1);
    xTaskCreatePinnedToCore(&DisplayShow, "DisplayShow", 2048, NULL, 2, &DisplayShowHandle, 1);
}