#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include <pthread.h>
#include <unistd.h>
#include <time.h>

// Maximos e Numeros, decididos pelo Usuario

int MAX_CUST_SOFA; // max de clientes no sofa
int MAX_CUST_FOOT; // max de clientes em pe
int NR_BARBERS;    // num de barbeiros
int NR_CUST;       // num de clientes (q tentarao entrar na barbearia)
int INTERVAL_CUST_ARRIVAL; // intervalo de tempo em q serah feito rand entre a chegada (ou criacao) de clientes
int HAIRCUT_TIME; // intervalo de tempo em q serah feito rand para cortar o cabelo (e ter cabelo cortado)
int ACCEPT_PAYMENT_TIME; // intervalo de tempo em q serah feito rand para um pagamento ser aceito
int NR_FULL_LOOPS; // num de loops q faremos para executar todo o codigo (uma barbearia com os mesmos valores em cada loop)

// ----------- Fila para Clientes em Pé (IDs) -----------

typedef struct {
    int* data;
    int capacity;
    int size;
    int front;
    int rear;
} QueueInt;

QueueInt queue_foot;

void init_queue_int(QueueInt* q, int capacity) {
    q->data = (int*)malloc(sizeof(int) * capacity);
    q->capacity = capacity;
    q->size = 0;
    q->front = 0;
    q->rear = 0;
}

int is_full_int(QueueInt* q) {
    return q->size == q->capacity;
}

int is_empty_int(QueueInt* q) {
    return q->size == 0;
}

void enqueue_int(QueueInt* q, int val) {
    if (is_full_int(q)) return;
    q->data[q->rear] = val;
    q->rear = (q->rear + 1) % q->capacity;
    q->size++;
}

int front_int(QueueInt* q) {
    if (is_empty_int(q)) return -1;
    return q->data[q->front];
}

int dequeue_int(QueueInt* q) {
    if (is_empty_int(q)) return -1;
    int val = q->data[q->front];
    q->front = (q->front + 1) % q->capacity;
    q->size--;
    return val;
}

// ----------- Struct do Cliente com Variáveis Sincronizadas -----------

typedef struct Cust_Sync {
    int cust_haircut_stages; // Alterado pelo Barbeiro: 0 quando o cliente ainda n esta pronto para executar getHairCut, 1 quando cliente esta pronto para executar getHairCut, 2 quando cabelo do cliente foi cortado e ele pode ir pagar
    pthread_mutex_t mutex_haircut; // protege cust_haircut_stages
    pthread_cond_t cond_cust_ready_to_getHairCut;
    pthread_cond_t cond_cust_hair_was_cut;
} Cust_Sync;

// ----------- Fila para Clientes no Sofá (cust_id + sync pointer) -----------

typedef struct {
    int cust_id;
    Cust_Sync* sync;
} SofaEntry;

typedef struct {
    SofaEntry* data;
    int capacity;
    int size;
    int front;
    int rear;
} QueueSofa;

QueueSofa queue_sofa;

void init_queue_sofa(QueueSofa* q, int capacity) {
    q->data = (SofaEntry*)malloc(sizeof(SofaEntry) * capacity);
    q->capacity = capacity;
    q->size = 0;
    q->front = 0;
    q->rear = 0;
}

int is_full_sofa(QueueSofa* q) {
    return q->size == q->capacity;
}

int is_empty_sofa(QueueSofa* q) {
    return q->size == 0;
}

void enqueue_sofa(QueueSofa* q, int cust_id, Cust_Sync* sync) {
    if (is_full_sofa(q)) return;
    q->data[q->rear].cust_id = cust_id;
    q->data[q->rear].sync = sync;
    q->rear = (q->rear + 1) % q->capacity;
    q->size++;
}

SofaEntry dequeue_sofa(QueueSofa* q) {
    SofaEntry empty = {-1, NULL};
    if (is_empty_sofa(q)) return empty;
    SofaEntry val = q->data[q->front];
    q->front = (q->front + 1) % q->capacity;
    q->size--;
    return val;
}

// ----------- Variáveis Numéricas do Programa -----------

int n_entered = 0; // conta quantos clientes entraram na loja. Serve pra fechar a loja antes q novos clientes entrem quando a meta do dia ja tiver sido batida
int shop_open = 0; // 0 quando Salao esta fechado, 1 quando esta aberto
int n_custs_in_shop = 0; // conta quantos clientes tem no Salao no momento

int custs_trying_to_pay = 0; // conta quantos clientes tentam pagar (executaram pay())
int receipts_to_receive = 0; // conta quantos pagamentos foram aceitos, mas clientes ainda nao viram (barbeiro ja fez acceptPayment() mas cliente ainda nao fez exit)
int cash_register_in_use = 0; // 0 quando o caixa esta livre, 1 quando caixa esta em uso

// ----------- Mutexes -----------

pthread_mutex_t mutex_general = PTHREAD_MUTEX_INITIALIZER; // protege queue_sofa, queue_foot, n_entered, shop_open e n_custs_in_shop
pthread_mutex_t mutex_payment = PTHREAD_MUTEX_INITIALIZER; // protege custs_trying_to_pay, receipts_to_receive e cash_register_idle

// ----------- Variáveis de Condição -----------

pthread_cond_t cond_free_sofa = PTHREAD_COND_INITIALIZER; // serve para indicar q tem espaco livre no Sofa
pthread_cond_t cond_barber_wake = PTHREAD_COND_INITIALIZER; // serve para acordar o barbeiro, indicando q um cliente sentou no Sofa ou q o Salao esta Fechado

pthread_cond_t cond_custs_trying_to_pay = PTHREAD_COND_INITIALIZER; // serve para cliente indicar os barbeiros de q alguem quer pagar
pthread_cond_t cond_receipts_to_receive = PTHREAD_COND_INITIALIZER; // serve para o barbeiro indicar aos clientes q um pagamento foi aceito
pthread_cond_t cond_cash_register_free = PTHREAD_COND_INITIALIZER; // serve para barbeiro indicar outros barbeiros q o caixa esta livre (ele aceitou um pagamento)

// ----------- Funções -----------
void enterShop(int cust_id) {
    printf("C%d: Entrei no Salao\n", cust_id);
}

void sitOnSofa(int cust_id) {
    printf("C%d: Sentei no Sofa\n", cust_id);
}

void getHairCut(int cust_id) {
    printf("C%d: Estou tendo meu Cabelo cortado\n", cust_id);
	sleep(rand() % (HAIRCUT_TIME + 1)); 
	// dorme para simular tempo do Corte
}

void pay(int cust_id) {
    printf("C%d: Estou preparado para Pagar\n", cust_id);
}

void acceptPayment(int barber_id) {
	sleep(rand() % (ACCEPT_PAYMENT_TIME + 1));
	// dorme por 1 segundo para simular tempo do Pagamento
	printf("B%d: Aceitei um pagamento\n", barber_id);
}

void cutHair(int barber_id, int cust_id) {
    printf("B%d: Estou cortando o cabelo do C%d\n", barber_id, cust_id);
	sleep(rand() % (HAIRCUT_TIME + 1));
	// dorme para simular tempo do Corte
}


// ----------- Thread do Cliente -----------
void* Customer(void* arg) {
    int cust_id = *((int*)arg);
    free(arg); // liberar memória alocada para o ID



    // ------------------------------ Tentando entrar no Salao ------------------------------

    pthread_mutex_lock(&mutex_general); // impede q valores sejam mudados enqt verificamos

    if (cust_id + 1 == NR_CUST) { // se o cliente eh o ultimo q tentara entrar
        printf("C%d: Sou o ultimo Cliente, Salao fecha agora\n", cust_id);
		shop_open = 0; // ele fecha a loja (ja q nenhum outro Cliente tentara entrar)
    }

    if (is_full_int(&queue_foot)) { // se salao cheio
        pthread_mutex_unlock(&mutex_general); // libera mutex antes de fechar thread
        printf("C%d: Fui embora, Salao estava Cheio\n", cust_id);
        pthread_exit(NULL);
    }

    enterShop(cust_id); // "C%d: Entrei no Salao"

    n_entered++;
    n_custs_in_shop++;
    enqueue_int(&queue_foot, cust_id);

    pthread_mutex_unlock(&mutex_general); // libera mutex dps do cliente entrar no salao

    // ------------------------------ Entrou no Salao com Sucesso ------------------------------




    // ------------------------------ Criando Variáveis Passadas para o Barbeiro na queue_sofa ------------------------------

    Cust_Sync* sync = malloc(sizeof(Cust_Sync)); // variaveis para passar ao barbeiro criadas
    sync->cust_haircut_stages = 0;
    pthread_mutex_init(&sync->mutex_haircut, NULL);
    pthread_cond_init(&sync->cond_cust_ready_to_getHairCut, NULL);
    pthread_cond_init(&sync->cond_cust_hair_was_cut, NULL);

    // ------------------------------ Variáveis Criadas ------------------------------




    // ------------------------------ Tentando sentar no Sofa ------------------------------

    pthread_mutex_lock(&mutex_general);

    while (is_full_sofa(&queue_sofa) || front_int(&queue_foot) != cust_id) { // enqt sofa cheio OU cliente nao eh o proximo a poder sentar
        pthread_cond_wait(&cond_free_sofa, &mutex_general); // espera enqt n tiver espaço no Sofa (acorda com broadcast do Barbeiro)
    }

    // Se a thread chegou nessa parte: Cliente eh o primeiro da fila em pe

    dequeue_int(&queue_foot);
    enqueue_sofa(&queue_sofa, cust_id, sync);

    sitOnSofa(cust_id); // "C%d: Sentei no Sofa"

    pthread_cond_signal(&cond_barber_wake); // acorda um barbeiro avisando q um cliente sentou no Sofa

    pthread_mutex_unlock(&mutex_general);

    // ------------------------------ Sentou no Sofa com Sucesso ------------------------------




    // ------------------------------ Tentando ter o cabelo cortado ------------------------------

    pthread_mutex_lock(&sync->mutex_haircut);

    while (sync->cust_haircut_stages == 0) {
        pthread_cond_wait(&sync->cond_cust_ready_to_getHairCut, &sync->mutex_haircut); // espera ate q barbeiro o "tire" do Sofa e avise
    }

    pthread_mutex_unlock(&sync->mutex_haircut);

    getHairCut(cust_id); // "C%d: Estou tendo meu Cabelo cortado"

    pthread_mutex_lock(&sync->mutex_haircut);

    while (sync->cust_haircut_stages != 2) {
        pthread_cond_wait(&sync->cond_cust_hair_was_cut, &sync->mutex_haircut); // espera ate q seu cabelo seja cortado pelo barbeiro para poder pagar
    }

    pthread_mutex_unlock(&sync->mutex_haircut);

    // ------------------------------ Cabelo do Cliente foi cortado ------------------------------




    // ------------------------------ Cliente tenta Pagar ------------------------------

    pay(cust_id); // "C%d: Estou preparado para Pagar"

    pthread_mutex_lock(&mutex_payment);

    custs_trying_to_pay++;
    pthread_cond_signal(&cond_custs_trying_to_pay); // cliente avisa os barbeiros de q quer pagar

    while (receipts_to_receive == 0) { // Esperar pagamento ser aceito para ir embora
        pthread_cond_wait(&cond_receipts_to_receive, &mutex_payment); // cliente espera o pagamento ser aceito
    }

    receipts_to_receive--;

    pthread_mutex_unlock(&mutex_payment);

    // ------------------------------ Pagamento Feito e Aceito ------------------------------



	
    // ------------------------------ Cliente vai embora de Cabelo Cortado ------------------------------

    pthread_mutex_lock(&mutex_general);

    n_custs_in_shop--;

	printf("C%d: Fui embora de cabelo cortado\n", cust_id);

    if (shop_open == 0 && n_custs_in_shop == 0) {
		printf("C%d: Barbeiros, Salao estava Fechado quando Sai\n", cust_id);
        pthread_cond_broadcast(&cond_barber_wake); // acorda todos os barbeiros, avisando q o Salao esta Fechado E nao tem ninguem no Salao
    }


    pthread_mutex_unlock(&mutex_general);

    pthread_mutex_destroy(&sync->mutex_haircut);
    pthread_cond_destroy(&sync->cond_cust_ready_to_getHairCut);
    pthread_cond_destroy(&sync->cond_cust_hair_was_cut);
    free(sync);

    pthread_exit(NULL);
}



// ----------- Thread do Barbeiro -----------
void* Barber(void* arg) {
    int barber_id = *((int*)arg);
    free(arg); // liberar memória alocada para o ID

    pthread_mutex_lock(&mutex_general);

    if (shop_open == 0) { // se salao nao estiver aberto
        shop_open = 1; // abrir salao
		printf("B%d: Abri o Salao\n", barber_id);
    }

    while (shop_open == 1 || n_custs_in_shop > 0) { // enqt salao ainda aberto OU ainda tiver cliente no salao
		

        // ------------------------------ Barbeiro Dorme ate: ter Cliente no Sofa OU ter Aviso de Salao Fechado ------------------------------
		while (is_empty_sofa(&queue_sofa) && (n_custs_in_shop > 0 || shop_open == 1)){
			// Barbeiro soh dorme se Sofa estiver vazio E (Salao tiver Cliente OU Salao estiver Aberto)
			
			// Achei confusa essa parte, ent aqui estao os casos em q o Barbeiro dorme e Pq
			// Casos q o Barbeiro Dorme:
				// Sofa esta vazio e Salao tem Cliente, mas Salao esta Fechado
					// Caso q o Ultimo cliente ja chegou ate a Porta (entrou ou n), mas tem cliente(s) para ser atendido(s)
				// Sofa esta vazio e Salao n tem Cliente, mas Salao esta Aberto
					// Caso q ele dorme ate mais cliente chegar
				// Sofa esta vazio e Salao tem Cliente e Salao esta Aberto
					// Caso q ele dorme ate um cliente sentar no Sofa
			pthread_cond_wait(&cond_barber_wake, &mutex_general);
		}

        if (is_empty_sofa(&queue_sofa)) { // Se nao tem Cliente no Sofa

            if (n_custs_in_shop == 0 && shop_open == 0) { // Se n tem mais Cliente E Salao ta fechado
                printf("B%d: Salao esta Vazio e Fechado\n", barber_id);
				break; // Barbeiro Para de Trabalhar (exit_thread)
            } else { // Se tem cliente no Salao OU Salao esta aberto
                continue; // volta a Dormir
            }
        }

        // ------------------------------ Tem Cliente no Sofa ------------------------------




        // ------------------------------ Barbeiro "tira" Cliente do Sofa, o Avisa disso e Avisa Clientes em Pe ------------------------------

        SofaEntry cust_variables = dequeue_sofa(&queue_sofa); // barbeiro "tira" cliente do Sofa

        pthread_cond_broadcast(&cond_free_sofa); // barbeiro avisa TODOS os clientes em pe q Sofa tem espaco livre


        int cust_id = cust_variables.cust_id;
        Cust_Sync* sync = cust_variables.sync;

		printf("B%d: Tirei o C%d do Sofa\n", barber_id, cust_id);

        pthread_mutex_unlock(&mutex_general);


        pthread_mutex_lock(&sync->mutex_haircut);

        sync->cust_haircut_stages = 1;

        pthread_cond_signal(&sync->cond_cust_ready_to_getHairCut); // avisa o cliente q ele foi tirado do Sofa e pode executar getHairCut

        pthread_mutex_unlock(&sync->mutex_haircut);

        // ------------------------------ Cliente esta na Cadeira para o Barbeiro cortar ------------------------------




        // ------------------------------ Barbeiro corta cabelo e Sinaliza pro Cliente sobre o corte ------------------------------

        cutHair(barber_id, cust_id); // "B%d: Estou cortando o cabelo do C%d"

        pthread_mutex_lock(&sync->mutex_haircut);

        sync->cust_haircut_stages = 2;

        pthread_cond_signal(&sync->cond_cust_hair_was_cut); // avisa o cliente q seu cabelo foi cortado, ent ele pode pagar

        pthread_mutex_unlock(&sync->mutex_haircut);

        // ------------------------------ Corte foi feito e sua Finalização avisada ao Cliente ------------------------------




        // ------------------------------ Barbeiro espera Cliente pagar e Caixa ficar disponivel, e ent Aceita Pagamento ------------------------------

        pthread_mutex_lock(&mutex_payment);

        while (custs_trying_to_pay == 0) {
            pthread_cond_wait(&cond_custs_trying_to_pay, &mutex_payment);
        }

        custs_trying_to_pay--;

        while (cash_register_in_use == 1) { // enqt Caixa estiver em uso
            pthread_cond_wait(&cond_cash_register_free, &mutex_payment);
        }

        cash_register_in_use = 1;
		printf("B%d: Peguei o Caixa\n", barber_id);

        pthread_mutex_unlock(&mutex_payment);

        acceptPayment(barber_id); // "B%d: Aceitei um pagamento"


        pthread_mutex_lock(&mutex_payment);

        cash_register_in_use = 0;

        pthread_cond_signal(&cond_cash_register_free); // avisa outro barbeiro q Caixa esta disponivel

        receipts_to_receive++;

        pthread_cond_signal(&cond_receipts_to_receive); // avisa clientes q um pagamento foi aceito

        pthread_mutex_unlock(&mutex_payment);

        // ------------------------------ Barbeiro aceitou o Pagamento e Pode voltar a trabalhar ------------------------------


        pthread_mutex_lock(&mutex_general);
    }

    pthread_mutex_unlock(&mutex_general);

    printf("B%d: Parei de Trabalhar\n", barber_id);
    pthread_exit(NULL);
}


// ----------- Função para Rodar um Loop -----------
void run_simulation() {
    // inicialização das filas
    init_queue_int(&queue_foot, MAX_CUST_FOOT);
    init_queue_sofa(&queue_sofa, MAX_CUST_SOFA);

    // zera as variáveis globais
    n_entered = 0;
    shop_open = 0;
    n_custs_in_shop = 0;
    custs_trying_to_pay = 0;
    receipts_to_receive = 0;
    cash_register_in_use = 0;

    pthread_t threadsCustomer[NR_CUST];
    pthread_t threadsBarber[NR_BARBERS];

    for (int i = 0; i < NR_BARBERS; i++) {
        int* id = malloc(sizeof(int));
        *id = i;
        pthread_create(&threadsBarber[i], NULL, Barber, id);
    }

    for (int i = 0; i < NR_CUST; i++) {
        sleep(rand() % (INTERVAL_CUST_ARRIVAL + 1));
        int* id = malloc(sizeof(int));
        *id = i;
        pthread_create(&threadsCustomer[i], NULL, Customer, id);
    }

    for (int i = 0; i < NR_CUST; i++) {
        pthread_join(threadsCustomer[i], NULL);
    }

    for (int i = 0; i < NR_BARBERS; i++) {
        pthread_join(threadsBarber[i], NULL);
    }

    printf("Numero de Clientes Atendidos = %d\n", n_entered);

    // liberar memória das filas
    free(queue_foot.data);
    free(queue_sofa.data);
}


// ----------- Função para pegar Valores do Arquivo de Configuração -----------
void load_config(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Erro ao abrir arquivo de configuração");
        exit(EXIT_FAILURE);
    }

    char line[256];
    char key[64];
    int value;

    while (fgets(line, sizeof(line), file)) {
        // Ignora linhas vazias e comentários (linha começando com //)
        if (line[0] == '\n' || line[0] == '\0' || line[0] == '/' || line[0] == '#')
            continue;

        // Remove comentário se houver (procura "//" e corta ali)
        char* comment_start = strstr(line, "//");
        if (comment_start != NULL) {
            *comment_start = '\0';  // Trunca a string ali
        }

        // Tenta extrair chave e valor
        if (sscanf(line, "%63s = %d", key, &value) == 2) {
            if (strcmp(key, "MAX_CUST_SOFA") == 0) MAX_CUST_SOFA = value;
            else if (strcmp(key, "MAX_CUST_FOOT") == 0) MAX_CUST_FOOT = value;
            else if (strcmp(key, "NR_BARBERS") == 0) NR_BARBERS = value;
            else if (strcmp(key, "NR_CUST") == 0) NR_CUST = value;
            else if (strcmp(key, "INTERVAL_CUST_ARRIVAL") == 0) INTERVAL_CUST_ARRIVAL = value;
            else if (strcmp(key, "HAIRCUT_TIME") == 0) HAIRCUT_TIME = value;
            else if (strcmp(key, "ACCEPT_PAYMENT_TIME") == 0) ACCEPT_PAYMENT_TIME = value;
            else if (strcmp(key, "NR_FULL_LOOPS") == 0) NR_FULL_LOOPS = value;
        }
    }

    fclose(file);
}


// ----------- Função main -----------
int main(void) {
    srand((unsigned)time(NULL));
        load_config("config.txt");

    // ----------- Verificação dos Valores das Variáveis -----------

    if (MAX_CUST_SOFA <= 0) {
        fprintf(stderr, "Erro: MAX_CUST_SOFA deve ser > 0\n");
        return EXIT_FAILURE;
    }

    if (MAX_CUST_FOOT <= 0) {
        fprintf(stderr, "Erro: MAX_CUST_FOOT deve ser >= 1\n");
        return EXIT_FAILURE;
    }

    if (NR_BARBERS <= 0) {
        fprintf(stderr, "Erro: NR_BARBERS deve ser > 0\n");
        return EXIT_FAILURE;
    }

    if (NR_CUST <= 0) {
        fprintf(stderr, "Erro: NR_CUST deve ser > 0\n");
        return EXIT_FAILURE;
    }

    if (INTERVAL_CUST_ARRIVAL < 0) {
        fprintf(stderr, "Erro: INTERVAL_CUST_ARRIVAL deve ser >= 0\n");
        return EXIT_FAILURE;
    }

    if (HAIRCUT_TIME < 0) {
        fprintf(stderr, "Erro: HAIRCUT_TIME deve ser >= 0\n");
        return EXIT_FAILURE;
    }

    if (ACCEPT_PAYMENT_TIME < 0) {
        fprintf(stderr, "Erro: ACCEPT_PAYMENT_TIME deve ser >= 0\n");
        return EXIT_FAILURE;
    }

    if (NR_FULL_LOOPS <= 0) {
        fprintf(stderr, "Erro: NR_FULL_LOOPS deve ser > 0\n");
        return EXIT_FAILURE;
    }


    for (int i = 0; i < NR_FULL_LOOPS; i++) {
        printf("\n===== Inicio do Loop %d =====\n", i + 1);
        run_simulation();
	printf("Numero de Clientes Atendidos = %d\n", n_entered);
        printf("===== Fim do Loop %d =====\n\n", i + 1);
    }

    return 0;
}

