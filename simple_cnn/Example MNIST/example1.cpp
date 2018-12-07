#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <algorithm>
#include "byteswap.h"
#include "CNN/cnn.h"
#include <chrono>

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#define SIZE 28
#define KERNEL 3

int dev;

using namespace std;

typedef std::chrono::milliseconds ms;

float train( vector<layer_t*>& layers, tensor_t<float>& data, tensor_t<float>& expected )
{
	for ( int i = 0; i < layers.size(); i++ )
	{
		if ( i == 0 )
			activate( layers[i], data );
		else
			activate( layers[i], layers[i - 1]->out );
	}

	tensor_t<float> grads = layers.back()->out - expected;

	for ( int i = layers.size() - 1; i >= 0; i-- )
	{
		if ( i == layers.size() - 1 )
			calc_grads( layers[i], grads );
		else
			calc_grads( layers[i], layers[i + 1]->grads_in );
	}

	for ( int i = 0; i < layers.size(); i++ )
	{
		fix_weights( layers[i] );
	}

	float err = 0;
	for ( int i = 0; i < grads.size.x * grads.size.y * grads.size.z; i++ )
	{
		float f = expected.data[i];
		if ( f > 0.5 )
			err += abs(grads.data[i]);
	}
	return err * 100;
}

float validate( vector<layer_t*>& layers, tensor_t<float>& data, tensor_t<float>& expected )
{
	for ( int i = 0; i < layers.size(); i++ )
	{
		if ( i == 0 )
			activate( layers[i], data );
		else
			activate( layers[i], layers[i - 1]->out );
	}

	tensor_t<float> grads = layers.back()->out - expected;
	
	float err = 0;
	for ( int i = 0; i < grads.size.x * grads.size.y * grads.size.z; i++ )
	{
		float f = expected.data[i];
		if ( f > 0.5 )
			err += abs(grads.data[i]);
	}
	return err * 100;
}

//***********************************************************************\\
//*************** FUNCOES DE ACESSO/LEITURA/ESCRITA NA FPGA *************\\
//***********************************************************************\\


// inicializar convolucao fpga, resetando registradores de convolucao
// e deixando clk em nivel logico baixo
void fpga_reset(int dev) {
	// binario 1 no bit 30 da entrada --> sinal de reset	
	// gera um pulsos com sinal de reset ativo, desativa reset
	// e deixa o clock em nivel logico baixo
	int data = 0x70000000; 
	write(dev, &data, 4); 
	data = 0xF0000000; 
	write(dev, &data, 4); 
	
	data = 0x70000000; 
	write(dev, &data, 4);
	data = 0xF0000000; 
	write(dev, &data, 4); 
}

// faz conversao de uma palavra de 16 bits para uma de 32 bits
// (tratamento para complemento de dois)
int sti(int s) {
	int i;
	
	// complemento de dois: repetir o bit mais significativo a esquerda
	if ((s & 0x00008000) == 0x00008000) return 0xFFFF0000 | s;
	else return 0x00000000 | s;
}

// retorna 1 se foi escrito um bit valido no endereco 'receive'
// senao, retorna 0
int fpga_read_write_data(int dev, int data, int* receive) {
	// sinal de clock passado no bit [31], ele vai estar em nivel logico alto
	// borda descida (subida no modulo conv)
	data = data & 0x0000FFFF;
	write(dev, &data, 4);

	// subida (descida no modulo conv)
	data = (data | 0x80000000);
	write(dev, &data, 4);

	// leitura do valor atualizado
	read(dev, receive, 4);
	
	// valor 0xFFFFFFFF inidica um resultado nao valido: se o resultado
	// for negativo a saida nao eh valida
	return ((*receive) == 0xFFFFFFFF) ? 0 : 1;
}

//***********************************************************************\\
//************************ FIM DAS FUNCOES DA FPGA **********************\\
//***********************************************************************\\

void my_convolution (vector<layer_t*>& layers, tensor_t<int>& data_fpga){
   // HARDWARE CONVOLUTION
   
   // ponteiro de arquivo global e ponteiro inicializado e fechado na main
   //int dev = open("/dev/de2i150_altera", O_RDWR); 
   int receive;
   int i, j, inc;
   int m = 0;
   int n = 0;
   int qtde_filtros = 1;

   // resetar registradores de convolucao
   fpga_reset(dev);

   // envio e leitura dos dados
   //printf("Envio e Leitura\n");
   for (int z = 0; z < qtde_filtros; z++) {
      for (i = 0; i < SIZE; i++){
      	for (j = 0; j < SIZE; j++){
      		inc = fpga_read_write_data(dev, data_fpga(i, j, 0), &receive);
      		//printf("%d ", data_fpga(i,j,0)); // test
      		
		// aplicar mascara e dividir por 100 (coeficientes reescalados para
		// fazer apenas operacoes com inteiros na fpga)
 		if (inc) {
      			layers[0]->out(m,n++,z) = ((float) sti(receive & 0x0000FFFF)) / 100.0;
      			if (n > (SIZE - KERNEL)) {
      				n = 0;
      				m++;
      			}
      		}
      	}
      }
      // loop adicional para pegar ultima linha
	for (i = 0; i < 2 * KERNEL; i++) {
		inc = fpga_read_write_data(dev, 1, &receive);
		if (inc) {
			layers[0]->out(m,n++,z) = sti(receive & 0x0000FFFF) / 1000.0;
			if (n > (SIZE - KERNEL)) {
      				n = 0;
      				m++;
      			}
		}
	}
   }
/*
   printf("Convoluted result: \n");
   for (i = 0; i < SIZE-KERNEL+1; i++) {
   	for (j = 0; j < SIZE-KERNEL+1; j++) {	
   		printf("%.2f ", layers[0]->out(i,j,0));
   	}
   	printf("\n");
   }
	getchar();*/
/*
   //Sending...
   printf("Imagem\n\n");
   for (int x = 0; x < 28; x++) {
       for (int y = 0; y < 28; y++) {
        //send_to_fpga(data_fpga(x,y,0);
        
        //Test
        printf("%d ", data_fpga(x,y,0));

       }
       printf("\n");
   }

   int qtd_filtros = 1;

   //Receiving...
   printf("Convolucao\n\n");
   for (int z = 0; z < qtd_filtros; z++) {
       for (int x = 0; x < 26; x++) {
           for (int y = 0; y < 26; y++) {
               //layers[0]->out(x,y,z) = receive_from_fpga();

               //Test
               //layers[0]->out(x,y,z) = y + x*26;
               //printf("%f ", layers[0]->out(x,y,z));

           }
           //printf("\n");
       }
   }
*/
}

void convolution (vector<layer_t*>& layers, tensor_t<float>& data){
    activate (layers[0], data);
}

void relu (vector<layer_t*>& layers, tensor_t<float>& data){
    activate(layers[1], layers[0]->out);
}

void pool (vector<layer_t*>& layers, tensor_t<float>& data){
    activate(layers[2], layers[1]->out);
}

void fc (vector<layer_t*>& layers, tensor_t<float>& data){
    activate(layers[3], layers[2]->out);
}

void forward( vector<layer_t*>& layers, tensor_t<float>& data )
{
    convolution(layers, data);
    relu(layers, data);
    pool(layers, data);
    fc(layers, data);

    /*
	for ( int i = 0; i < layers.size(); i++ )
	{
		if ( i == 0 )
			activate( layers[i], data );
		else
			activate( layers[i], layers[i - 1]->out );
	}
    */

}

struct case_t
{
	tensor_t<float> data;
    tensor_t<int> data_fpga;
	tensor_t<float> out;
};

uint8_t* read_file( const char* szFile )
{
	ifstream file( szFile, ios::binary | ios::ate );
	streamsize size = file.tellg();
	file.seekg( 0, ios::beg );

	if ( size == -1 )
		return nullptr;

	uint8_t* buffer = new uint8_t[size];
	file.read( (char*)buffer, size );
	return buffer;
}

vector<case_t> read_train_cases()
{
	vector<case_t> cases;

	uint8_t* train_image = read_file( "train-images.idx3-ubyte" );
	uint8_t* train_labels = read_file( "train-labels.idx1-ubyte" );

	//Relacionado com arquivo MNIST (http://yann.lecun.com/exdb/mnist/)
	//Leitura do tamanho em Big-endian
	uint32_t case_count = byteswap_uint32( *(uint32_t*)(train_image + 4) );
	for ( int i = 0; i < case_count; i++ )
	{
		//Tamanho dos dados para ler do arquivo MNIST (dado = 28x28, saída=10 classes)
		case_t c {tensor_t<float>( 28, 28, 1 ), tensor_t<int>( 28, 28, 1 ), tensor_t<float>( 10, 1, 1 )};

		//Leitura pixel a pixel (row-wise)
		uint8_t* img = train_image + 16 + i * (28 * 28);
		
		//Leitura dos labels
		uint8_t* label = train_labels + 8 + i;

		for ( int x = 0; x < 28; x++ ){
			for ( int y = 0; y < 28; y++ ){
				c.data( x, y, 0 ) = img[x + y * 28] / 255.f;
                c.data_fpga( x, y, 0 ) = img[x + y * 28]; 
            }
        }

		for ( int b = 0; b < 10; b++ )
			c.out( b, 0, 0 ) = *label == b ? 1.0f : 0.0f;

		cases.push_back( c );
	}
	delete[] train_image;
	delete[] train_labels;

	return cases;
}

vector<case_t> read_test_cases()
{
	vector<case_t> cases;

	uint8_t* test_image = read_file( "t10k-images-idx3-ubyte" );
	uint8_t* test_labels = read_file( "t10k-labels-idx1-ubyte" );

	//Relacionado com arquivo MNIST (http://yann.lecun.com/exdb/mnist/)
	//Leitura do tamanho em Big-endian
	uint32_t case_count = byteswap_uint32( *(uint32_t*)(test_image + 4) );
	for ( int i = 0; i < case_count; i++ )
	{
		//Tamanho dos dados para ler do arquivo MNIST (dado = 28x28, saída=10 classes)
		case_t c {tensor_t<float>( 28, 28, 1 ), tensor_t<int>( 28, 28, 1 ), tensor_t<float>( 10, 1, 1 )};

		//Leitura pixel a pixel (row-wise)
		uint8_t* img = test_image + 16 + i * (28 * 28);
		
		//Leitura dos labels
		uint8_t* label = test_labels + 8 + i;

		for ( int x = 0; x < 28; x++ )
			for ( int y = 0; y < 28; y++ )
				c.data( x, y, 0 ) = img[x + y * 28] / 255.f;

		for ( int b = 0; b < 10; b++ )
			c.out( b, 0, 0 ) = *label == b ? 1.0f : 0.0f;

		cases.push_back( c );
	}
	delete[] test_image;
	delete[] test_labels;

	return cases;
}


int main()
{
	//Leitura da base de dados - treino
	vector<case_t> cases = read_train_cases();

    vector<case_t> cases_test = read_test_cases();
	
	dev = open("/dev/de2i150_altera", O_RDWR);
	printf("Device: %d\n", dev);
   	if (dev < 0) {
 	      printf("Erro ao acessar barramento!\n");
  	     return -1;
  	}

	vector<layer_t*> layers;

	//1 = passo do filtro
	//3 = extensão do filtro (tamanho do filtro)
	//1 = número de filtros
	//size = tamanho da entrada
	conv_layer_t * layer1 = new conv_layer_t( 1, 3, 1, cases[0].data.size );	
	relu_layer_t * layer2 = new relu_layer_t( layer1->out.size );
	
	//1 = Passo do filtro
	//1 = Tamanho do filtro
	pool_layer_t * layer3 = new pool_layer_t( 1, 1, layer2->out.size );			
	fc_layer_t * layer4 = new fc_layer_t(layer3->out.size, 10);				

	layers.push_back( (layer_t*)layer1 );
	layers.push_back( (layer_t*)layer2 );
	layers.push_back( (layer_t*)layer3 );
	layers.push_back( (layer_t*)layer4 );

	double amse = 0, amse_test = 0;
	long ic = 0, ic_test = 0;

	for ( int ep = 1; ep <= 1; ep++ )
	{
		for ( case_t& t : cases )
		{
			float xerr = train( layers, t.data, t.out );
			amse += xerr;

			ic++;

			// if ( GetAsyncKeyState( VK_F1 ) & 0x8000 )
			// {
			//	   printf( "err=%.4f%\n", amse / ic  );
			//	   goto end;
			// }
		}
		cout << "Train " << ep << " err=" << amse/ic << endl;
/*
        for ( case_t& t : cases_test )
		{
			float xerr = validate( layers, t.data, t.out );
			amse_test += xerr;

			ic_test++;

			// if ( GetAsyncKeyState( VK_F1 ) & 0x8000 )
			// {
			//	   printf( "err=%.4f%\n", amse / ic  );
			//	   goto end;
			// }
		}
		cout << "Validation " << ep << " err=" << amse_test/ic_test << endl;
*/		
	}
	
	cout << "Your filters:" << endl;
	
	for (int i = 0; i < layer1->filters.size(); i++){
		cout << "--------------------------------"<<endl;
		for (int j = 0; j < layer1->filters.at(i).size.x; j++){
			for (int k = 0; k < layer1->filters.at(i).size.y; k++){
				cout << layer1->filters.at(i).get(j,k,0) << " ";
			}
			cout << endl;
		}
		cout << "--------------------------------"<<endl;			
	}
	
	long matrix[10][10];
	int previsto, fato;
	float max;
	
	for (int i = 0; i < 10; i++){
		for(int j = 0; j < 10; j++){
			matrix[i][j] = 0;
		}
	}

    long hits = 0;
    long iteration = 0;
    double elapsed_conv = 0;
    double elapsed_relu = 0;
    double elapsed_pool = 0;
    double elapsed_fullcon = 0;

    std::chrono::duration<double> elapsed_cv, elapsed_rl, elapsed_pl, elapsed_fc;

    for ( case_t& t : cases_test )
	{
        auto start_cv = std::chrono::high_resolution_clock::now();
        my_convolution(layers, t.data_fpga);
        //convolution(layers, t.data);
        auto finish_cv = std::chrono::high_resolution_clock::now();
        
        elapsed_cv += finish_cv - start_cv;
        std::chrono::milliseconds cv_ms = std::chrono::duration_cast<ms>(elapsed_cv);

        elapsed_conv += cv_ms.count();

        auto start_relu = std::chrono::high_resolution_clock::now();
        relu(layers, t.data);
        auto finish_relu = std::chrono::high_resolution_clock::now();

        elapsed_rl += finish_relu - start_relu;
        std::chrono::milliseconds rl_ms = std::chrono::duration_cast<ms>(elapsed_rl);

        elapsed_relu += rl_ms.count();

        auto start_pl = std::chrono::high_resolution_clock::now();
        pool(layers, t.data);
        auto finish_pl = std::chrono::high_resolution_clock::now();

        elapsed_pl += finish_pl - start_pl;
        std::chrono::milliseconds pl_ms = std::chrono::duration_cast<ms>(elapsed_pl);

        elapsed_pool += pl_ms.count();

        auto start_fc = std::chrono::high_resolution_clock::now();
        fc(layers, t.data);
        auto finish_fc = std::chrono::high_resolution_clock::now();

        elapsed_fc += finish_fc - start_fc;
        std::chrono::milliseconds fc_ms = std::chrono::duration_cast<ms>(elapsed_fc);

        elapsed_fullcon += fc_ms.count();

//		forward( layers, t.data );
		tensor_t<float>& out = layers.back()->out;
		
		for ( int i = 0; i < 10; i++ )
		{
			if (t.out(i,0,0) == 1.0){
				fato = i;
				break;
			}
		}
		
		max = out(0, 0, 0);
		previsto = 0;
		
		for (int i = 1; i < 10; i++){
			if (out(i,0,0) > max){
				max = out(i, 0, 0);
				previsto = i;
			}	
		}
		
		matrix[fato][previsto] += 1;

        if (fato == previsto) hits += 1;
        iteration += 1;
	}
	
	for (int i = 0; i < 10; i++){
		for(int j = 0; j < 10; j++){
			printf("%lu ", matrix[i][j]);
		}
		printf("\n");
	}

    cout << "Conv : " << elapsed_conv/iteration << endl;
    cout << "Relu : " << elapsed_relu/iteration << endl;
    cout << "Pool : " << elapsed_pool/iteration << endl;
    cout << "FC : " << elapsed_fullcon/iteration << endl;

    cout << "Hits: " << ((double) hits)/iteration << endl;
	return 1;
	
    /*
	while ( true )
	{
		uint8_t * data = read_file( "test.ppm" );

		if ( data )
		{
			uint8_t * usable = data;

			while ( *(uint32_t*)usable != 0x0A353532 )
				usable++;

#pragma pack(push, 1)
			struct RGB
			{
				uint8_t r, g, b;
			};
#pragma pack(pop)

			RGB * rgb = (RGB*)usable;

			tensor_t<float> image(28, 28, 1);
			for ( int i = 0; i < 28; i++ )
			{
				for ( int j = 0; j < 28; j++ )
				{
					RGB rgb_ij = rgb[i * 28 + j];
					image( j, i, 0 ) = (((float)rgb_ij.r
							     + rgb_ij.g
							     + rgb_ij.b)
							    / (3.0f*255.f));
				}
			}

			forward( layers, image );
			tensor_t<float>& out = layers.back()->out;
			for ( int i = 0; i < 10; i++ )
			{
				printf( "[%i] %f\n", i, out( i, 0, 0 )*100.0f );
			}

			delete[] data;
		}

		struct timespec wait;
		wait.tv_sec = 1;
		wait.tv_nsec = 0;
		nanosleep(&wait, nullptr);
	}
	*/
	close(dev);
	return 0;
}
