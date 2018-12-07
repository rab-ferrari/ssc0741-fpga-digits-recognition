#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <algorithm>
#include "byteswap.h"
#include "CNN/cnn.h"

using namespace std;

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


void forward( vector<layer_t*>& layers, tensor_t<float>& data )
{
	for ( int i = 0; i < layers.size(); i++ )
	{
		if ( i == 0 )
			activate( layers[i], data );
		else
			activate( layers[i], layers[i - 1]->out );
	}
}

struct case_t
{
	tensor_t<float> data;
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
		case_t c {tensor_t<float>( 28, 28, 1 ), tensor_t<float>( 10, 1, 1 )};

		//Leitura pixel a pixel (row-wise)
		uint8_t* img = train_image + 16 + i * (28 * 28);
		
		//Leitura dos labels
		uint8_t* label = train_labels + 8 + i;

		for ( int x = 0; x < 28; x++ )
			for ( int y = 0; y < 28; y++ )
				c.data( x, y, 0 ) = img[x + y * 28] / 255.f;

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
		case_t c {tensor_t<float>( 28, 28, 1 ), tensor_t<float>( 10, 1, 1 )};

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
    int max_step = 7;

	//Leitura da base de dados - treino
	vector<case_t> cases = read_train_cases();

    vector<case_t> cases_test = read_test_cases();

    for (int i = 1; i <= max_step; i++) {

        cout << i << " Step\n" << endl;

        vector<layer_t*> layers;

        conv_layer_t * layer1 = new conv_layer_t( i, 7, 10, cases[0].data.size );		
        relu_layer_t * layer2 = new relu_layer_t( layer1->out.size );
        
        pool_layer_t * layer3 = new pool_layer_t( 1, 6, layer2->out.size );			
        fc_layer_t * layer4 = new fc_layer_t(layer3->out.size, 10);					

        layers.push_back( (layer_t*)layer1 );
        layers.push_back( (layer_t*)layer2 );
        layers.push_back( (layer_t*)layer3 );
        layers.push_back( (layer_t*)layer4 );


        double amse = 0, amse_test = 0;
        long ic = 0, ic_test = 0;

        //for ( long ep = 0; ep < 100000; )
        for ( long ep = 0; ep < 1; )
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

            ep++;
        }
        layers.clear();
    }
	
	return 0;
}
