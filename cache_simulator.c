
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <winsock2.h>

//Estrutura que representa a Cache
typedef struct {
    int offset;
    int index;
    int tag;
    int validity;
    int fifoQueue; //Contém um valor que coloca o endereço em uma posição da fila usando um contador
    int lruCounter; //Contém um valor que conta a quantidade de hits sobre o endereço
} Cache;

int main(int argc, char* argv[]) {
    //O programa deve aceitar apenas 7 argumentos    
    if ( argc != 7 ) {
        printf( "Número incorreto de argumentos. Utilize:\n" );
        printf( "./cache_simulator <nsets> <bsize> <assoc> <substituição> <flag_saida> arquivo_de_entrada.bin\n" );
        exit( EXIT_FAILURE );
    }

    //Leitura dos parâmetros de entrada
    int nsets = atoi( argv[1] ); //Número de conjuntos
    int bsize = atoi( argv[2] ); //Tamanho do bloco em bytes
    int assoc = atoi( argv[3] ); //Nível de associatividade
    char substitution_policy = *argv[4]; //Política de substituição
    int flag_saida = atoi( argv[5] ); //Formato de saída (0 para saída padrão e 1 para saida rotulada)
    char* arquivo_entrada = argv[6]; //Arquivo de entrada

    //Verifica se a política de substituicao foi bem introduzida
    if (substitution_policy != 'F' && substitution_policy != 'L' && substitution_policy != 'R'){
        exit( EXIT_FAILURE );
    }

    //Set-up para traducao de Big Endian para Little Endian (apenas para sistemas Windows Little Endian)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }

    //Variáveis de amostra
    int accessCounter = 0;
    int hitCounter = 0;
    int missComp = 0;
    int missConf = 0;
    int missCapa = 0;

    //Variáveis de suporte
    int cacheSize = nsets*bsize*assoc;
    int offsetSize = log2( bsize );
    int indexSize = log2( nsets );
    if (nsets == 1){
        indexSize = log2( assoc );
    }
    int tagSize = 32 - offsetSize - indexSize;
    int fifoCounter = 0;
  
    //Alocação de memória para a cache
    Cache* cache = (Cache*)malloc(nsets * assoc * sizeof(Cache));
    if (!cache) {
        printf("Error: Memory allocation failed.\n");
        exit(EXIT_FAILURE);
    }

    //Inicialização das linhas da cache em base à associatividade
    if (assoc == 1) { // Mapeamento direto quando o nível de associatividade é 1
        for (int i = 0; i < nsets; i++) {
            cache[i].offset = 0;
            cache[i].index = i; //Índice sequencial simples
            cache[i].tag = -1;
            cache[i].validity = 0;
            cache[i].fifoQueue = 0;
            cache[i].lruCounter = 0;
        }
    }

    if (nsets == 1) { // Totalmente associativo quando o número de conjuntos é 1
        for (int i = 0; i < nsets * assoc; i++) {
            cache[i].offset = 0;
            cache[i].index = 0; //Índices não são usados pois todas as linhas pertencem ao mesmo índice
            cache[i].tag = -1;
            cache[i].validity = 0;
            cache[i].fifoQueue = 0;
            cache[i].lruCounter = 0;
        }
    }

    if (assoc != nsets && assoc !=1){ // Parcialmente associativo
        for (int i = 0; i < nsets * assoc; i++) {
            cache[i].offset = 0;
            cache[i].index = i % nsets; //Índices são compartilhados pelo mesmo conjunto e o tamanho dos conjuntos são determinados pela associatividade
            cache[i].tag = -1;
            cache[i].validity = 0;
            cache[i].fifoQueue = 0;
            cache[i].lruCounter = 0;
        }
    }

    //Abre o arquivo em modo de apenas leitura de binários e lê os endereços
    FILE* inputFile = fopen( arquivo_entrada, "rb" );
    if ( inputFile == NULL ) {
        printf( "Erro ao abrir o arquivo de entrada.\n" );
        exit( EXIT_FAILURE );
    }

    unsigned int address;

    while (fread(&address, sizeof(unsigned int), 1, inputFile)) {

        address = ntohl(address); //Traduz o Endian do formato do arquivo lido

        // Cálculo dos valores do deslocamento (offset), índice (index) e rótulo (tag)
        unsigned int offsetMask = (1 << offsetSize) - 1; //Operações binárias para formar uma máscara (cria um valor binário com offsetSize 0's e 1 na frente, logo substrai 1 e obtem-se uma sequência de 1's com 0 na frente)
        unsigned int offset = address & offsetMask; //Aplicação da máscara usando o operador AND binário
            
        unsigned int indexMask = ((1 << indexSize) - 1) << offsetSize;
        unsigned int index = (address & indexMask) >> offsetSize;

        unsigned int tag = address >> (indexSize + offsetSize);
        if( nsets == 1 ){ //O rótulo para totalmente associativa é maior pois não inclui o índice
            tag = address >> offsetSize;
        }

        //Acesso à cache em base à associatividade
        if ( assoc==1 ){ // Mapeamento direto
            accessCounter++; //Conta todo acesso feito à cache
            bool accessVerifier = false; //Verifica se a busca pelo endereço deve continuar
            for (int i = 0; i < nsets; i++){
                if (cache[i].index == index){
                    if (cache[i].validity == 0){ //Falha obrigatória
                        cache[i].validity = 1;       
                        cache[i].offset = offset;
                        cache[i].tag = tag;
                        missComp++;
                        accessVerifier = true;
                    } else if (cache[i].validity == 1){   
                        if (cache[i].tag == tag){ //Acerto
                            hitCounter++;
                            accessVerifier = true;
                        } else {  //Falha de conflito
                            cache[i].offset = offset;
                            cache[i].tag = tag;

                            missConf++;
                            accessVerifier = true;
                        }
                    }
                }
            }
            if (accessVerifier == false){ //Falha de capacidade (não encontrou uma linha com índice correspondente)
                missCapa++;
                int i = rand() % (nsets + 1); //Substituição aleatória
                cache[i].offset = offset;
                cache[i].tag = tag;
            }
        }
        if (nsets == 1){ //Totalmente associativa
            accessCounter++;
            fifoCounter++; //Aumenta para cada acesso à memória como a 'accessCounter'
            bool accessVerifier = false;
            for (int i = 0; i < assoc; i++){
                if (cache[i].validity == 0){
                    cache[i].validity = 1;       
                    cache[i].offset = offset;
                    cache[i].tag = tag;
                    cache[i].fifoQueue = fifoCounter; //Valor atualizado para todo acesso
                    cache[i].lruCounter++; //Valor atualizado para todo acesso
                    accessVerifier = true;
                    missComp++;
                    break;
                } else if (cache[i].tag == tag && cache[i].validity == 1){ //Acerto
                    hitCounter++;
                    cache[i].fifoQueue = fifoCounter;
                    cache[i].lruCounter++;
                    accessVerifier = true;
                    break;
                }
            }
            if (accessVerifier == false) { //Toda falha não obrigatória na cache totalmente associativa é de capacidade
                if (substitution_policy == 'R'){ //Política de substituição 'Random'
                    missCapa++;
                    int i = rand() % assoc;
                    cache[i].offset = offset;
                    cache[i].tag = tag;
                    cache[i].fifoQueue = fifoCounter;
                } else if (substitution_policy == 'F'){ //Política de substituição 'First In First Out'
                    int j = cache[0].fifoQueue, k;
                    for (int i = 0; i < assoc; i++){
                        if (j < cache[i].fifoQueue){ //Busca o endereço com o valor 'fifoQueue' mais baixo referente ao item mais antigo da fila
                            k = i;
                            j = cache[i].fifoQueue;
                        }
                    }
                    cache[k].validity = 1;
                    cache[k].tag = tag;
                    cache[k].offset = offset;
                    cache[k].fifoQueue = fifoCounter; //Recebe o valor atual (maior)
                    accessVerifier = true;
                    missCapa++;
                } else if (substitution_policy == 'L'){ //Política de substituição 'Least Recently Used'
                    int j = cache[0].lruCounter, k = 0;
                    for (int i = 0; i < assoc; i++){
                        if (j < cache[i].lruCounter){ //Busca o endereço com o valor 'lruCounter' mais baixo referente ao item menos acessado da memória
                            k = i;
                            j = cache[i].lruCounter;
                        }
                    }
                    cache[k].validity = 1;
                    cache[k].tag = tag;
                    cache[k].offset = offset;
                    cache[k].lruCounter = 0; //Seu valor referente à quantidade de acessos é resetado
                    accessVerifier = true;
                    missCapa++;
                }
            }
        }
        if (assoc != 1 && nsets != 1 ){ // Parcialmente associativa
            accessCounter++;
            bool accessVerifier = false;
            int setIndex = index * assoc;

            for (int i = setIndex; i < setIndex + assoc; i++) { //Loop apenas dentro do conjunto com mesmo índice
                if (cache[i].validity == 0) { // Falha obrigatória
                    cache[i].validity = 1;
                    cache[i].offset = offset;
                    cache[i].tag = tag;
                    accessVerifier = true;
                    missComp++;
                    break;
                } else if (cache[i].tag == tag && cache[i].validity == 1) { // Acerto
                    hitCounter++;
                    accessVerifier = true;
                    break;
                }
            }
            if (accessVerifier == false) {
                int l =0;
                for (int i = setIndex; i < setIndex + assoc; i++){
                    if (cache[i].offset != offset){ //Checando por falhas de conflito
                        l++;
                    }
                }
                if (l == assoc){
                    int k = setIndex;
                    if (substitution_policy == 'R'){
                        k += rand() % assoc;
                        cache[k].validity = 1;
                        cache[k].tag = tag;
                        cache[k].offset = offset;
                        cache[k].fifoQueue = fifoCounter;
                        accessVerifier = true;
                        missConf++;
                    } else if (substitution_policy == 'F'){
                        int j = cache[k * assoc].fifoQueue;
                        for (int i = setIndex; i < setIndex + assoc; i++){
                            if(cache[i].fifoQueue < j) {
                                j = cache[i].fifoQueue;
                                k = i;
                            }
                        }
                        cache[k].validity = 1;
                        cache[k].tag = tag;
                        cache[k].offset = offset;
                        cache[k].fifoQueue = fifoCounter;
                        accessVerifier = true;
                        missConf++;
                    } else if (substitution_policy == 'L'){
                        int j = cache[setIndex].lruCounter;
                        for (int i = setIndex; i < setIndex + assoc; i++){
                            if (cache[i].lruCounter < j) {
                                j = cache[i].lruCounter;
                                k = i;
                            }
                        }
                        cache[k].validity = 1;
                        cache[k].tag = tag;
                        cache[k].offset = offset;
                        cache[k].lruCounter = 0;
                        accessVerifier = true;
                        missConf++;
                    }
                }
            }
            if (accessVerifier == false){
                int k = setIndex;
                if (substitution_policy == 'R'){
                    k += rand() % assoc;
                    cache[k].validity = 1;
                    cache[k].tag = tag;
                    cache[k].offset = offset;
                    cache[k].fifoQueue = fifoCounter;
                    accessVerifier = true;
                    missCapa++;
                } else if (substitution_policy == 'F'){
                    int j = cache[k * assoc].fifoQueue;
                    for (int i = setIndex; i < setIndex + assoc; i++){
                        if(cache[i].fifoQueue < j) {
                            j = cache[i].fifoQueue;
                            k = i;
                        }
                    }
                    cache[k].validity = 1;
                    cache[k].tag = tag;
                    cache[k].offset = offset;
                    cache[k].fifoQueue = fifoCounter;
                    accessVerifier = true;
                    missCapa++;
                } else if (substitution_policy == 'L'){
                    int j = cache[setIndex].lruCounter;
                    for (int i = setIndex; i < setIndex + assoc; i++){
                        if (cache[i].lruCounter < j) {
                            j = cache[i].lruCounter;
                            k = i;
                        }
                    }
                    cache[k].validity = 1;
                    cache[k].tag = tag;
                    cache[k].offset = offset;
                    cache[k].lruCounter = 0;
                    accessVerifier = true;
                    missCapa++;
                }
            }
        }
    }

    fclose( inputFile );
    WSACleanup();

    //Imprimir o relatório
    if ( flag_saida == 0 ) {
        printf("\nTotal de acessos = %d \nTaxa de hits = %d (%.2f%%)\nTaxa de miss = %d (%.2f%%) \n", accessCounter, hitCounter, ((float)hitCounter / accessCounter) * 100, accessCounter - hitCounter, (1.0 - (float)hitCounter / accessCounter) *100);
        printf("Miss compulsorios: %d (%.2f%%) \nMiss de capacidade: %d (%.2f%%) \nMiss de conflito: %d (%.2f%%)", missComp, ((float)missComp / (accessCounter - hitCounter))*100, missCapa, ((float)missCapa / (accessCounter - hitCounter))*100, missConf, ((float)missConf / (accessCounter - hitCounter))*100);
    } else if (flag_saida == 1) {
        printf("%d %.4f %.4f %.2f %.2f %.2f\n",
               accessCounter, // Total de acessos
               (float)hitCounter / accessCounter, // Taxa de hit
               1.0 - (float)hitCounter / accessCounter, // Taxa de miss
               (float)missComp / (accessCounter - hitCounter), // Taxa de miss compulsório
               (float)missCapa / (accessCounter - hitCounter), // Taxa de miss de capacidade
               (float)missConf / (accessCounter - hitCounter)); // Taxa de miss de conflito
    }

    free(cache);
    
    return 0;
}
