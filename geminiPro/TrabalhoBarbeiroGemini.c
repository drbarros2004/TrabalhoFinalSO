#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>     // Para sleep
#include <time.h>       // Para time (usado no srand)
#include <stdbool.h>    // Para usar 'true' e 'false'
#include <string.h>     // Para strcmp, strtok
#include <ctype.h>      // Para isspace

// --- Variáveis Globais e Suas Finalidades ---

// Variáveis configuráveis (serão lidas do config.txt)
int MAX_CUST_SOFA;
int MAX_CUST_FOOT;
int NR_BARBERS;
int NR_CUST;
int MAX_INTERVAL_CUST_ARRIVAL_SECS;
int MAX_HAIRCUT_SECS;
int MAX_PAYMENT_PREP_SECS;
int MAX_PAYMENT_ACCEPT_SECS;
int NR_FULL_LOOPS;

// Capacidade total da barbearia (calculada e atribuída em init_global_vars)
int TOTAL_SHOP_CAPACITY;

// Enum para o estado do barbeiro para logs mais claros
typedef enum {
    BARBER_IDLE,
    BARBER_CUTTING_HAIR,
    BARBER_PROCESSING_PAYMENT
} BarberState;

BarberState* barber_state;

// --- Estrutura e Funções de Fila Simples ---
typedef struct {
    int* data;
    int head;
    int tail;
    int size;
    int capacity;
} SimpleQueue;

void init_queue(SimpleQueue* q, int capacity) {
    q->data = (int*)malloc(sizeof(int) * capacity);
    if (!q->data) {
        perror("Falha ao alocar memória para a fila");
        exit(EXIT_FAILURE);
    }
    q->head = 0;
    q->tail = -1;
    q->size = 0;
    q->capacity = capacity;
}

int enqueue(SimpleQueue* q, int item) {
    if (q->size == q->capacity) {
        return -1; // Fila cheia
    }
    q->tail = (q->tail + 1) % q->capacity;
    q->data[q->tail] = item;
    q->size++;
    return 0; // Sucesso
}

int dequeue(SimpleQueue* q) {
    if (q->size == 0) {
        return -1; // Fila vazia
    }
    int item = q->data[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->size--;
    return item;
}

int front(SimpleQueue* q) {
    if (q->size == 0) {
        return -1; // Fila vazia
    }
    return q->data[q->head];
}

bool is_empty(SimpleQueue* q) {
    return q->size == 0;
}

int get_size(SimpleQueue* q) {
    return q->size;
}

bool contains_queue(SimpleQueue* q, int item) {
    if (q->size == 0) return false;
    int current_index = q->head;
    for (int i = 0; i < q->size; i++) {
        if (q->data[current_index] == item) {
            return true;
        }
        current_index = (current_index + 1) % q->capacity;
    }
    return false;
}

// Numeros e Estruturas Variaveis, usados e manipulados pelo Programa
SimpleQueue queue_sofa;
SimpleQueue queue_foot;
SimpleQueue queue_payments_pending;

int current_customers_in_shop = 0;
int customers_in_barber_chairs = 0;

int last_cust_at_door = 0; // 0 = ainda não chegou, 1 = chegou

// Para o caixa (somente um)
int cash_register_idle = 1;

// Variáveis para sincronização ESPECÍFICA de cada cliente (usamos ID 1-base)
int* cust_hair_was_cut;
pthread_mutex_t* mutex_cust_hair_was_cut;
pthread_cond_t* cond_cust_hair_was_cut;

int* cust_ready_to_getHairCut;
pthread_mutex_t* mutex_cust_ready_to_getHairCut;
pthread_cond_t* cond_cust_ready_to_getHairCut;

int* cust_payment_status;
pthread_mutex_t* mutex_cust_payment_status;
pthread_cond_t* cond_cust_payment_accepted;

// Mutexes e Variáveis de Condição Globais
pthread_mutex_t mutex_n_entered_queues; // Protege last_cust_at_door, current_customers_in_shop, customers_in_barber_chairs, queue_sofa, queue_foot
pthread_mutex_t mutex_cash_register_idle;
pthread_mutex_t mutex_payments_queue;

pthread_cond_t cond_free_sofa;
pthread_cond_t cond_sitOnSofa;
pthread_cond_t cond_cash_register_free;
pthread_cond_t cond_payments_pending;

// --- Funções Auxiliares ---
void initialize_random_seed() {
    srand(time(NULL));
}

void sleep_random_time_in_seconds(int max_secs) {
    if (max_secs <= 0) return;
    int delay_secs = rand() % max_secs;
    sleep(delay_secs);
}

// Funções de logging e simulação das ações
void enterShop_log(int cust_id) {
    printf("C%d: Entrei no Salão\n", cust_id);
}

void standOnLine_log(int cust_id){
    printf("C%d: Estou na fila em pé\n", cust_id);
}

void sitOnSofa_log(int cust_id) {
    printf("C%d: Sentei no Sofá\n", cust_id);
}

void sitOnChair_log(int cust_id) {
    printf("C%d: Estou sentado na cadeira do barbeiro\n", cust_id);
}

void getHairCut_client(int cust_id) {
    printf("C%d: Estou tendo o cabelo cortado\n", cust_id); // Mantendo o log do cliente
}

void pay_log(int cust_id) {
    printf("C%d: Estou preparado para pagar\n", cust_id);
}

void barber_cutHair_log(int barber_id, int cust_id) {
    barber_state[barber_id] = BARBER_CUTTING_HAIR;
    printf("B%d: Estou cortando o cabelo do C%d\n", barber_id, cust_id);
}

void barber_acceptPayment_log(int barber_id, int cust_id) {
    barber_state[barber_id] = BARBER_PROCESSING_PAYMENT;
    printf("B%d: Processando pagamento do C%d\n", barber_id, cust_id);
}

// --- Funções de Leitura do Arquivo de Configuração ---
// Função para remover espaços em branco do início e fim de uma string
char* trim_whitespace(char* str) {
    char *end;

    // Trim leading space
    while(isspace((unsigned char)*str)) str++;

    if(*str == 0)  // All spaces?
        return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;

    // Write new null terminator
    *(end+1) = 0;

    return str;
}

void read_config(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        perror("Erro ao abrir o arquivo de configuração");
        exit(EXIT_FAILURE);
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char* trimmed_line = trim_whitespace(line);
        if (strlen(trimmed_line) == 0 || trimmed_line[0] == '#') { // Ignora linhas vazias e comentários
            continue;
        }

        char* key = strtok(trimmed_line, "=");
        char* value_str = strtok(NULL, "=");

        if (key && value_str) {
            key = trim_whitespace(key);
            value_str = trim_whitespace(value_str);
            int value = atoi(value_str);

            if (strcmp(key, "MAX_CUST_SOFA") == 0) {
                MAX_CUST_SOFA = value;
            } else if (strcmp(key, "MAX_CUST_FOOT") == 0) {
                MAX_CUST_FOOT = value;
            } else if (strcmp(key, "NR_BARBERS") == 0) {
                NR_BARBERS = value;
            } else if (strcmp(key, "NR_CUST") == 0) {
                NR_CUST = value;
            } else if (strcmp(key, "MAX_INTERVAL_CUST_ARRIVAL_SECS") == 0) {
                MAX_INTERVAL_CUST_ARRIVAL_SECS = value;
            } else if (strcmp(key, "MAX_HAIRCUT_SECS") == 0) {
                MAX_HAIRCUT_SECS = value;
            } else if (strcmp(key, "MAX_PAYMENT_PREP_SECS") == 0) {
                MAX_PAYMENT_PREP_SECS = value;
            } else if (strcmp(key, "MAX_PAYMENT_ACCEPT_SECS") == 0) {
                MAX_PAYMENT_ACCEPT_SECS = value;
            } else if (strcmp(key, "NR_FULL_LOOPS") == 0) {
                NR_FULL_LOOPS = value;
            } else {
                fprintf(stderr, "Aviso: Chave desconhecida no arquivo de configuração: %s\n", key);
            }
        }
    }
    fclose(file);
}

// Função de inicialização global (chamada uma vez no main OU no reset)
void init_global_vars() {
    // Estas variáveis são alocadas e inicializadas CADA VEZ
    // que init_global_vars é chamada.
    // Isso é importante para resetar o estado da simulação.

    TOTAL_SHOP_CAPACITY = MAX_CUST_SOFA + MAX_CUST_FOOT + NR_BARBERS;

    // Reinicializa as filas
    init_queue(&queue_sofa, MAX_CUST_SOFA);
    init_queue(&queue_foot, MAX_CUST_FOOT);
    init_queue(&queue_payments_pending, NR_CUST); // Capacidade baseada no número total de clientes

    // Alocações de arrays dinâmicos
    barber_state = (BarberState*)malloc(sizeof(BarberState) * (NR_BARBERS + 1));
    cust_hair_was_cut = (int*)malloc(sizeof(int) * (NR_CUST + 1));
    mutex_cust_hair_was_cut = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t) * (NR_CUST + 1));
    cond_cust_hair_was_cut = (pthread_cond_t*)malloc(sizeof(pthread_cond_t) * (NR_CUST + 1));

    cust_ready_to_getHairCut = (int*)malloc(sizeof(int) * (NR_CUST + 1));
    mutex_cust_ready_to_getHairCut = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t) * (NR_CUST + 1));
    cond_cust_ready_to_getHairCut = (pthread_cond_t*)malloc(sizeof(pthread_cond_t) * (NR_CUST + 1));

    cust_payment_status = (int*)malloc(sizeof(int) * (NR_CUST + 1));
    mutex_cust_payment_status = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t) * (NR_CUST + 1));
    cond_cust_payment_accepted = (pthread_cond_t*)malloc(sizeof(pthread_cond_t) * (NR_CUST + 1));

    if (!barber_state || !cust_hair_was_cut || !mutex_cust_hair_was_cut || !cond_cust_hair_was_cut ||
        !cust_ready_to_getHairCut || !mutex_cust_ready_to_getHairCut || !cond_cust_ready_to_getHairCut ||
        !cust_payment_status || !mutex_cust_payment_status || !cond_cust_payment_accepted) {
        perror("Falha na alocação de memória para arrays globais. Encerrando.");
        exit(EXIT_FAILURE);
    }

    // Inicializa estados e mutexes/condvars
    for(int i = 1; i <= NR_BARBERS; i++) {
        barber_state[i] = BARBER_IDLE;
    }

    for (int i = 1; i <= NR_CUST; i++) {
        cust_hair_was_cut[i] = 0;
        pthread_mutex_init(&mutex_cust_hair_was_cut[i], NULL);
        pthread_cond_init(&cond_cust_hair_was_cut[i], NULL);

        cust_ready_to_getHairCut[i] = 0;
        pthread_mutex_init(&mutex_cust_ready_to_getHairCut[i], NULL);
        pthread_cond_init(&cond_cust_ready_to_getHairCut[i], NULL);

        cust_payment_status[i] = 0;
        pthread_mutex_init(&mutex_cust_payment_status[i], NULL);
        pthread_cond_init(&cond_cust_payment_accepted[i], NULL);
    }

    // Inicializa mutexes e condvars globais
    pthread_mutex_init(&mutex_n_entered_queues, NULL);
    pthread_mutex_init(&mutex_cash_register_idle, NULL);
    pthread_mutex_init(&mutex_payments_queue, NULL);

    pthread_cond_init(&cond_free_sofa, NULL);
    pthread_cond_init(&cond_sitOnSofa, NULL);
    pthread_cond_init(&cond_cash_register_free, NULL);
    pthread_cond_init(&cond_payments_pending, NULL);

    // Reinicializa contadores globais
    current_customers_in_shop = 0;
    customers_in_barber_chairs = 0;
    last_cust_at_door = 0;
    cash_register_idle = 1;
}

// Função para liberar memória e destruir mutexes/condvars entre as execuções
void cleanup_global_vars() {
    // Libera a memória das filas
    free(queue_sofa.data);
    free(queue_foot.data);
    free(queue_payments_pending.data);

    // Destroi e libera arrays dinâmicos
    for (int i = 1; i <= NR_CUST; i++) {
        pthread_mutex_destroy(&mutex_cust_hair_was_cut[i]);
        pthread_cond_destroy(&cond_cust_hair_was_cut[i]);
        pthread_mutex_destroy(&mutex_cust_ready_to_getHairCut[i]);
        pthread_cond_destroy(&cond_cust_ready_to_getHairCut[i]);
        pthread_mutex_destroy(&mutex_cust_payment_status[i]);
        pthread_cond_destroy(&cond_cust_payment_accepted[i]);
    }

    free(barber_state);
    free(cust_hair_was_cut);
    free(mutex_cust_hair_was_cut);
    free(cond_cust_hair_was_cut);
    free(cust_ready_to_getHairCut);
    free(mutex_cust_ready_to_getHairCut);
    free(cond_cust_ready_to_getHairCut);
    free(cust_payment_status);
    free(mutex_cust_payment_status);
    free(cond_cust_payment_accepted);

    // Destroi mutexes e condvars globais
    pthread_mutex_destroy(&mutex_n_entered_queues);
    pthread_mutex_destroy(&mutex_cash_register_idle);
    pthread_mutex_destroy(&mutex_payments_queue);
    pthread_cond_destroy(&cond_free_sofa);
    pthread_cond_destroy(&cond_sitOnSofa);
    pthread_cond_destroy(&cond_cash_register_free);
    pthread_cond_destroy(&cond_payments_pending);
}

// --- Threads ---

void* customer_thread(void* arg) {
    int cust_id = *(int*)arg;

    pthread_mutex_lock(&mutex_n_entered_queues);

    if (cust_id == NR_CUST) { // Se o ID do cliente atual é igual ao número total de clientes
        last_cust_at_door = 1; // Marca que o último cliente chegou à porta
        printf("C%d: Sou o último cliente a chegar à porta\n", cust_id);
    }

    // --- Tentando entrar no Salão ---
    if (current_customers_in_shop >= TOTAL_SHOP_CAPACITY) {
        pthread_mutex_unlock(&mutex_n_entered_queues);
        printf("C%d: Fui embora, Salao estava Cheio\n", cust_id);

        if (last_cust_at_door == 1) {
            pthread_cond_broadcast(&cond_sitOnSofa);
        }
        pthread_exit(NULL);
    }

    enterShop_log(cust_id);
    current_customers_in_shop++;

    if (enqueue(&queue_foot, cust_id) == 0) { // Verifica se conseguiu entrar na fila de pé
        standOnLine_log(cust_id);
    }

    pthread_mutex_unlock(&mutex_n_entered_queues);
    // --- Entrou no Salão com Sucesso ---


    // --- Tentando sentar no Sofá ---
    pthread_mutex_lock(&mutex_n_entered_queues);

    while (get_size(&queue_sofa) == MAX_CUST_SOFA || (!is_empty(&queue_foot) && front(&queue_foot) != cust_id)) {
        pthread_cond_wait(&cond_free_sofa, &mutex_n_entered_queues);
    }

    if (!is_empty(&queue_foot) && front(&queue_foot) == cust_id) {
        dequeue(&queue_foot);
    }

    enqueue(&queue_sofa, cust_id);

    sitOnSofa_log(cust_id);
    pthread_cond_signal(&cond_sitOnSofa);

    pthread_mutex_unlock(&mutex_n_entered_queues);
    // --- Sentou no Sofá com Sucesso ---


    // --- Tentando ter o cabelo cortado ---
    pthread_mutex_lock(&mutex_cust_ready_to_getHairCut[cust_id]);
    while (cust_ready_to_getHairCut[cust_id] == 0) {
        pthread_cond_wait(&cond_cust_ready_to_getHairCut[cust_id], &mutex_cust_ready_to_getHairCut[cust_id]);
    }
    cust_ready_to_getHairCut[cust_id] = 0;
    pthread_mutex_unlock(&mutex_cust_ready_to_getHairCut[cust_id]);

    getHairCut_client(cust_id);

    pthread_mutex_lock(&mutex_cust_hair_was_cut[cust_id]);
    while (cust_hair_was_cut[cust_id] == 0) {
        pthread_cond_wait(&cond_cust_hair_was_cut[cust_id], &mutex_cust_hair_was_cut[cust_id]);
    }
    cust_hair_was_cut[cust_id] = 0;
    pthread_mutex_unlock(&mutex_cust_hair_was_cut[cust_id]);
    // --- Cabelo do Cliente foi cortado ---


    // --- Cliente tenta Pagar ---
    pay_log(cust_id);
    sleep_random_time_in_seconds(MAX_PAYMENT_PREP_SECS);

    pthread_mutex_lock(&mutex_payments_queue);
    enqueue(&queue_payments_pending, cust_id);
    pthread_cond_signal(&cond_payments_pending);
    pthread_mutex_unlock(&mutex_payments_queue);

    pthread_mutex_lock(&mutex_cust_payment_status[cust_id]);
    while (cust_payment_status[cust_id] == 0) {
        pthread_cond_wait(&cond_cust_payment_accepted[cust_id], &mutex_cust_payment_status[cust_id]);
    }
    cust_payment_status[cust_id] = 0;
    pthread_mutex_unlock(&mutex_cust_payment_status[cust_id]);
    // --- Pagamento Feito e Aceito ---


    // --- Cliente vai embora de Cabelo Cortado ---
    pthread_mutex_lock(&mutex_n_entered_queues);
    current_customers_in_shop--;

    if (last_cust_at_door == 1 && current_customers_in_shop == 0) {
        printf("C%d: Barbeiros, o ultimo cliente ja chegou ate a porta antes\n", cust_id);
        pthread_cond_broadcast(&cond_sitOnSofa);
        pthread_cond_broadcast(&cond_payments_pending);
    }

    pthread_mutex_unlock(&mutex_n_entered_queues);

    printf("C%d: Fui embora de cabelo cortado\n", cust_id);
    pthread_exit(NULL);
}


void* barber_thread(void* arg) {
    int barber_id = *(int*)arg;
    int cust_id_to_serve;
    int payment_cust_id;

    while (true) {
        pthread_mutex_lock(&mutex_n_entered_queues);

        // --- Condição para o barbeiro encerrar o trabalho ---
        // Se o último cliente já chegou E não há clientes em espera (filas ou cadeiras) OU pagamentos pendentes
        if (last_cust_at_door == 1 &&
            is_empty(&queue_sofa) &&
            is_empty(&queue_foot) &&
            customers_in_barber_chairs == 0 &&
            is_empty(&queue_payments_pending)) {
            pthread_mutex_unlock(&mutex_n_entered_queues);
            break;
        }

        // --- Espera por um cliente no sofá (se não houver) ---
        while (is_empty(&queue_sofa)) {
            // Verifica condição de término antecipado (dentro do mutex para ler estado consistente)
            if (last_cust_at_door == 1 &&
                is_empty(&queue_sofa) &&
                is_empty(&queue_foot) &&
                customers_in_barber_chairs == 0 &&
                is_empty(&queue_payments_pending)) {
                pthread_mutex_unlock(&mutex_n_entered_queues);
                goto end_barber_thread;
            }

            if (barber_state[barber_id] == BARBER_IDLE) {
                printf("B%d: Sem clientes no sofa. Dormindo...\n", barber_id);
            }
            pthread_cond_wait(&cond_sitOnSofa, &mutex_n_entered_queues);
        }

        cust_id_to_serve = dequeue(&queue_sofa);
        customers_in_barber_chairs++;
        sitOnChair_log(cust_id_to_serve); // Log para cliente sentando na cadeira
        pthread_cond_signal(&cond_free_sofa);
        pthread_mutex_unlock(&mutex_n_entered_queues);

        // --- Fase de Corte de Cabelo ---
        pthread_mutex_lock(&mutex_cust_ready_to_getHairCut[cust_id_to_serve]);
        cust_ready_to_getHairCut[cust_id_to_serve] = 1;
        pthread_cond_signal(&cond_cust_ready_to_getHairCut[cust_id_to_serve]);
        pthread_mutex_unlock(&mutex_cust_ready_to_getHairCut[cust_id_to_serve]);

        barber_cutHair_log(barber_id, cust_id_to_serve);
        sleep_random_time_in_seconds(MAX_HAIRCUT_SECS);

        pthread_mutex_lock(&mutex_cust_hair_was_cut[cust_id_to_serve]);
        cust_hair_was_cut[cust_id_to_serve] = 1;
        pthread_cond_signal(&cond_cust_hair_was_cut[cust_id_to_serve]);
        pthread_mutex_unlock(&mutex_cust_hair_was_cut[cust_id_to_serve]);


        // --- Fase de Pagamento ---
        // Adquire mutexes na ordem GLOBAL -> ESPECÍFICO para evitar deadlock
        pthread_mutex_lock(&mutex_n_entered_queues); // Protege contadores globais para verificação de término
        pthread_mutex_lock(&mutex_payments_queue); // Protege a fila de pagamentos

        while (is_empty(&queue_payments_pending)) {
            // Verifica condição de término antes de esperar por pagamento
            if (last_cust_at_door == 1 &&
                is_empty(&queue_sofa) &&
                is_empty(&queue_foot) &&
                customers_in_barber_chairs == 0 &&
                is_empty(&queue_payments_pending)) {
                pthread_mutex_unlock(&mutex_payments_queue);
                pthread_mutex_unlock(&mutex_n_entered_queues);
                goto end_barber_thread;
            }

            printf("B%d: Esperando cliente para pagamento...\n", barber_id);
            pthread_cond_wait(&cond_payments_pending, &mutex_payments_queue);
        }
        payment_cust_id = dequeue(&queue_payments_pending);

        pthread_mutex_unlock(&mutex_payments_queue);
        pthread_mutex_unlock(&mutex_n_entered_queues);


        // Tenta adquirir o caixa (mutex_cash_register_idle é independente dos outros mutexes de fila/contadores)
        pthread_mutex_lock(&mutex_cash_register_idle);
        while (cash_register_idle == 0) {
            printf("B%d: Caixa ocupado, esperando...\n", barber_id);
            pthread_cond_wait(&cond_cash_register_free, &mutex_cash_register_idle);
        }
        cash_register_idle = 0;
        pthread_mutex_unlock(&mutex_cash_register_idle);

        barber_acceptPayment_log(barber_id, payment_cust_id);
        sleep_random_time_in_seconds(MAX_PAYMENT_ACCEPT_SECS);

        pthread_mutex_lock(&mutex_cash_register_idle);
        cash_register_idle = 1;
        pthread_cond_broadcast(&cond_cash_register_free);
        pthread_mutex_unlock(&mutex_cash_register_idle);

        pthread_mutex_lock(&mutex_cust_payment_status[payment_cust_id]);
        cust_payment_status[payment_cust_id] = 1;
        pthread_cond_signal(&cond_cust_payment_accepted[payment_cust_id]);
        pthread_mutex_unlock(&mutex_cust_payment_status[payment_cust_id]);

        pthread_mutex_lock(&mutex_n_entered_queues);
        customers_in_barber_chairs--;
        pthread_mutex_unlock(&mutex_n_entered_queues);

        barber_state[barber_id] = BARBER_IDLE;
    }
end_barber_thread:
    barber_state[barber_id] = BARBER_IDLE;
    printf("B%d: Indo embora, sem mais clientes para atender.\n", barber_id);
    pthread_exit(NULL);
}


int main() {
    // 1. Ler arquivo de configuração UMA VEZ
    read_config("config.txt");

    // 2. Loop para rodar a simulação múltiplas vezes
    for (int loop = 1; loop <= NR_FULL_LOOPS; loop++) {
        printf("\n--- Rodada de Simulacao #%d ---\n", loop);

        // Inicializa/Reseta todas as variáveis globais, mutexes e condvars
        // antes de cada nova rodada
        init_global_vars();
        initialize_random_seed(); // Para garantir que os tempos aleatórios sejam diferentes a cada rodada

        pthread_t barber_threads[NR_BARBERS];
        pthread_t customer_threads[NR_CUST];

        int barber_ids[NR_BARBERS];
        int customer_ids[NR_CUST];

        // Cria threads de barbeiros
        for (int i = 0; i < NR_BARBERS; i++) {
            barber_ids[i] = i + 1;
            if (pthread_create(&barber_threads[i], NULL, barber_thread, &barber_ids[i]) != 0) {
                perror("Erro ao criar thread de barbeiro");
                exit(EXIT_FAILURE);
            }
        }

        // Cria threads de clientes
        for (int i = 0; i < NR_CUST; i++) {
            customer_ids[i] = i + 1;
            sleep_random_time_in_seconds(MAX_INTERVAL_CUST_ARRIVAL_SECS);
            if (pthread_create(&customer_threads[i], NULL, customer_thread, &customer_ids[i]) != 0) {
                perror("Erro ao criar thread de cliente");
                exit(EXIT_FAILURE);
            }
        }

        // Espera por todas as threads de clientes terminarem
        for (int i = 0; i < NR_CUST; i++) {
            pthread_join(customer_threads[i], NULL);
        }

        // Espera por todas as threads de barbeiros terminarem
        for (int i = 0; i < NR_BARBERS; i++) {
            pthread_join(barber_threads[i], NULL);
        }

        printf("\nSimulacao da Barbearia Encerrada para Rodada #%d.\n", loop);

        // Limpa e libera recursos para a próxima rodada
        cleanup_global_vars();
    }

    printf("\nTodas as rodadas de simulacao concluidas.\n");

    return 0;
}