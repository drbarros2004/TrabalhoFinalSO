Global{ // não é uma função de verdade, serve apenas para mostrar as variáveis globais e o que elas significam
	
	// Máximos e números (decididos pelo usuário)

	MAX_CUST_SOFA         // max. de clientes no sofá
	MAX_CUST_FOOT         // max. de clientes em pé
	NR_BARBERS            // num. de barbeiros
	NR_CUST               // num. de clientes (que tentarão entrar na barbearia)
	NR_CUST_DAY           // num. de clientes para se atender por dia, salao eh fechado dps disso
	INTERVAL_CUST_ARRIVAL // intervalo de tempo em que será feito rand entre a chegada (ou criação) de clientes
	
	// Números e estruturas Variaveis (usados e manipulados pelo programa)
	queue_sofa = [] (max size = MAX_CUST_SOFA) // fila que guarda IDs dos clientes conforme sentam no sofá
	queue_foot = [] (max size = MAX_CUST_FOOT) // fila que guarda IDs dos clientes conforme entram no salão
	
	n_entered = 0 <= NR_CUST_DAY      // conta quantos clientes entraram na loja. Serve pra fechar a loja antes que novos clientes entrem quando a meta do dia já tiver sido batida
	custs_trying_to_pay = 0           // conta quantos clientes tentam pagar (executaram pay())
	receipts_to_receive = 0           // conta quantos pagamentos foram aceitos, mas clientes ainda não viram (barbeiro ja fez acceptPayment() mas cliente ainda nao fez exit())
	cash_register_idle = 1            // 0 quando o caixa está em uso (não esta ocioso), 1 quando caixa esta livre
	
	// Mutexes das Variaveis, usados e manipulados pelo Programa
	mutex_n_entered_queues       // protege queue_sofa, queue_foot e n_entered
	                             // todos eles têm o mesmo mutex porque estão conectados: quando um cliente entra,
                               // aumentamos o n_entered e mudamos a queue_foot; quando um cliente vai para o sofá, tiramos ele da queue_foot e botamos na queue_sofa;
	mutex_custs_trying_to_pay    // protege custs_trying_to_pay
	mutex_receipts_to_receive    // protege receipts_to_receive
	mutex_cash_register_idle     // protege cash_register_idle

	// Variaveis de condição (usadas e manipuladas pelo programa)

	cond_free_sofa             // serve para indicar que existe espaco livre no sofá
	cond_sitOnSofa             // serve para indicar ao barbeiro que um cliente sentou no sofá (caso o barbeiro esteja esperando ter algum cliente)
	cond_custs_trying_to_pay   // serve para o cliente indicar aos barbeiros de que alguém quer pagar
	cond_receipts_to_receive   // serve para o barbeiro indicar aos clientes que um pagamento foi aceito 
	cond_cash_register_free    // serve para barbeiro indicar outros barbeiros que o caixa está livre (ele aceitou um pagamento)
	
}

Customer(cust_id){

	// -------------------------- Tentando entrar no Salao ------------------------------
	lock -> mutex_n_entered_queues // impede que valores sejam mudados enquanto verificamos
	
	if (n_entered == NR_CUST_DAY){     // se meta foi atingida (se salão foi fechado)
		unlock -> mutex_n_entered_queues // libera mutex antes de fechar thread
		"C%d: Fui embora, Salao estava Fechado"
		exit_thread()
	}
	
	if (queue_foot.size == MAX_CUST_FOOT){ // se salão está cheio
		unlock -> mutex_n_entered_queues     // libera mutex antes de fechar thread
		"C%d: Fui embora, Salao estava Cheio"
		exit_thread()
	}

	enterShop(cust_id) // "C%d: Entrei no Salao"
	n_entered++;
	queue_foot.enqueue(cust_id)
	
	unlock -> mutex_n_entered_queues // libera mutex depois do cliente entrar no salão

	// ------------------------------ Entrou no salão com Sucesso ------------------------------
	
	
	// ------------------------------ Criando variáveis passadas para o Barbeiro na queue_sofa ------------------------------
	cust_hair_was_cut = 0
	mutex_cust_hair_was_cut
	cond_cust_hair_was_cut
	
	cust_ready_to_getHairCut = 0
	mutex_cust_ready_to_getHairCut
	cond_cust_ready_to_getHairCut
	
	// ------------------------------ Tentando sentar no sofá ------------------------------

	lock -> mutex_n_entered_queues
	
	while (queue_sofa.size == MAX_CUST_SOFA || queue_foot.front != cust_id){ // enquanto sofá está cheio OU cliente não é o proximo a poder sentar
		wait(cond_free_sofa, mutex_n_entered_queues)    // espera enquanto não tiver espaço no sofá (acorda com broadcast do Barbeiro) 
	}

	// Se a thread chegou nessa parte: Cliente é o primeiro da fila em pé

	queue_foot.dequeue()
	queue_sofa.enqueue([cust_id, cust_hair_was_cut, mutex_cust_hair_was_cut, cond_cust_hair_was_cut, cust_ready_to_getHairCut, mutex_cust_ready_to_getHairCut, cond_cust_ready_to_getHairCut])
	
	sitOnSofa(cust_id)     // "C%d: Sentei no Sofa"
	signal(cond_sitOnSofa) // avisa aos barbeiros q um cliente sentou no Sofa
	
	unlock -> mutex_n_entered_queues

	// ------------------------------ Sentou no sofá com sucesso ------------------------------



	// ------------------------------ Tentando ter o cabelo cortado ------------------------------
	lock -> mutex_cust_ready_to_getHairCut
	while (cust_ready_to_getHairCut == 0){
		wait(cond_cust_ready_to_getHairCut, mutex_cust_ready_to_getHairCut) // espera até que barbeiro o "retire" do sofá e avise
	}
	unlock -> mutex_cust_ready_to_getHairCut
	
	getHairCut(cust_id) // "C%d: Estou tendo meu Cabelo cortado"
	
	lock -> mutex_cust_hair_was_cut
	while (cust_hair_was_cut == 0){
		wait(cond_cust_hair_was_cut, mutex_cust_hair_was_cut) // espera até que seu cabelo seja cortado pelo barbeiro para poder pagar
	}
	unlock -> mutex_cust_hair_was_cut

	// ------------------------------ Cabelo do Cliente foi cortado ------------------------------
	

	// ------------------------------ Cliente tenta Pagar ------------------------------
	pay(cust_id) // "C%d: Estou preparado para Pagar"
	lock -> mutex_custs_trying_to_pay
	custs_trying_to_pay++
	signal(cond_custs_trying_to_pay) // cliente avisa aos barbeiros que quer pagar
	unlock -> mutex_custs_trying_to_pay
	
	// Esperar pagamento ser aceito para ir embora
	lock -> mutex_receipts_to_receive
	while (receipts_to_receive == 0){
		wait(cond_receipts_to_receive, mutex_receipts_to_receive) // cliente espera o pagamento ser aceito
	}
	receipts_to_receive--
	unlock -> mutex_receipts_to_receive
	// ------------------------------ Pagamento feito e aceito ------------------------------
	
	
	
	
	// ------------------------------ Cliente vai embora de Cabelo Cortado ------------------------------
	"C%d: Fui embora de cabelo cortado"
	exit_thread()
}

Barber(barber_id){
	
	lock -> mutex_n_entered_queues 
	while (n_entered <= NR_CUST_DAY){ // enquanto num. de clientes que entraram for menor que a meta (salão ainda aberto)
		unlock -> mutex_n_entered_queues 
	
	
		// ------------------------------ Barbeiro "tira" Cliente do sofá, o avisa disso e avisa Clientes em pé ------------------------------		
		lock -> mutex_n_entered_queues 
		while (queue_sofa.size == 0){ 
			wait(cond_sitOnSofa, mutex_n_entered_queues) // barbeiro espera sofá ter alguém
		}
		
		cust_variables = queue_sofa.pop() // barbeiro "tira" cliente do sofá
		broadcast(cond_free_sofa)         // barbeiro avisa TODOS os clientes em pé que o sofá tem espaco livre
		
		// ESSES VALORES NÃO SÃO COPIAS, SÃO OS LOCAIS DA MEMORIA EM SI (em C mudar para ponteiros)
		cust_id = cust_variables[0] 
		cust_hair_was_cut = cust_variables[1] 
		mutex_cust_hair_was_cut = cust_variables[2]
		cond_cust_hair_was_cut = cust_variables[3]
		cust_ready_to_getHairCut = cust_variables[4]
		mutex_cust_ready_to_getHairCut = cust_variables[5]
		cond_cust_ready_to_getHairCut = cust_variables[6]
		
		unlock -> mutex_n_entered_queues
		
		lock -> mutex_cust_ready_to_getHairCut
		cust_ready_to_getHairCut = 1
		signal(cond_cust_ready_to_getHairCut) // avisa o cliente que ele foi tirado do sofá e pode ter seu cabelo cortado
		unlock -> mutex_cust_ready_to_getHairCut
		
		// ------------------------------ Cliente está na Cadeira para o Barbeiro cortar ------------------------------
		
	
		// ------------------------------ Barbeiro corta cabelo e Sinaliza pro Cliente sobre o corte ------------------------------
		cutHair(barber_id, cust_id) // "B%d: Estou cortando o cabelo do C%d"
		
		lock -> mutex_cust_hair_was_cut
		cust_hair_was_cut = 1
		signal(cond_cust_hair_was_cut) // avisa o cliente que seu cabelo foi cortado, então ele pode pagar
		unlock -> mutex_cust_hair_was_cut
		// ------------------------------ Corte foi feito e sua finalização avisada ao Cliente ------------------------------
		
	
		// ------------------------------ Barbeiro espera Cliente pagar e caixa ficar disponível e aceita pagamento ------------------------------

		lock -> mutex_custs_trying_to_pay
		while (custs_trying_to_pay == 0){
			wait(cond_custs_trying_to_pay, mutex_custs_trying_to_pay)
		}
		custs_trying_to_pay--
		unlock -> mutex_custs_trying_to_pay
		
		
		lock -> mutex_cash_register_idle
		while (cash_register_idle == 0){
			wait(cond_cash_register_free, mutex_cash_register_idle)
		}
		acceptPayment() // "B%d: Aceitei um pagamento"
		cash_register_idle = 1
		signal(cond_cash_register_free) // avisa outro barbeiro que o caixa está disponivel
		unlock -> mutex_cash_register_idle


		
		lock -> mutex_receipts_to_receive
		receipts_to_receive++
		signal(cond_receipts_to_receive) // avisa clientes que um pagamento foi aceito
		unlock -> mutex_receipts_to_receive
		// ------------------------------ Barbeiro aceitou o pagamento e pode voltar a trabalhar ------------------------------
		
		lock -> mutex_n_entered_queues 
	}
	unlock -> mutex_n_entered_queues 
	
	exit_thread()
}
