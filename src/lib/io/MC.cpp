#include "../Partio.h"
#include "../core/ParticleHeaders.h"
#include "endian.h" // read/write big-endian file
#include "ZIP.h" // for zip file

#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
#include <memory>

namespace Partio{

using namespace std;
// TODO: convert this to use iterators like the rest of the readers/writers

std::string GetString(std::istream& input, unsigned int size){
    char* tmp = new char [size];
    input.read(tmp, size);
    std::string result(tmp);

    // fix read tag error (ex: DBLA --> DBLAi, why !!)
    if(result.size() > size){
        result.resize(size);
    }

    delete [] tmp;
    return result;
}

typedef struct{
    std::string name;
    std::string type;
    unsigned int numParticles;
    unsigned int blocksize;
} Attribute_Header;


bool ReadAttrHeader(std::istream& input, Attribute_Header& attribute){
    char tag[4];
    input.read(tag, 4); // CHNM

    int chnmSize;
    read<BIGEND>(input, chnmSize);
    if(chnmSize%4 > 0){
        chnmSize = chnmSize - chnmSize%4 + 4;
    }
    attribute.name = GetString(input, chnmSize);
    attribute.name = attribute.name.substr(attribute.name.find_first_of("_")+1);

    input.read(tag, 4); // SIZE
    int dummy;
    read<BIGEND>(input, dummy); // 4

    read<BIGEND>(input, attribute.numParticles);

    attribute.type = GetString(input, 4);

    read<BIGEND>(input, attribute.blocksize);

    return true;
}

static const int MC_MAGIC = ((((('F'<<8)|'O')<<8)|'R')<<8)|'4';
static const int HEADER_SIZE = 56;

ParticlesDataMutable* readMC(const char* filename, const bool headersOnly){

    std::auto_ptr<std::istream> input(Gzip_In(filename,std::ios::in|std::ios::binary));
    if(!*input){
        std::cerr << "Partio: Unable to open file " << filename << std::endl;
        return 0;
    }

    int magic;
    read<BIGEND>(*input, magic);
    if(MC_MAGIC != magic){
        std::cerr << "Partio: Magic number '" << magic << "' of '" << filename << "' doesn't match mc magic '" << MC_MAGIC << "'" << std::endl;
        return 0;
    }

    int headerSize;
    read<BIGEND>(*input, headerSize);
    input->seekg((int)input->tellg() + headerSize);

    /*int dummy; // tmp1, tmp2, num1, tmp3, tmp4, num2, num3, tmp5, num4, num5, blockTag
    for(int i = 0; i < 10; i++){ 
        read<BIGEND>(*input, dummy);
        //std::cout << dummy << std::endl;
    }*/

    char tag[4];
    input->read(tag, 4); // FOR4

    int blockSize;
    read<BIGEND>(*input, blockSize);

    // Allocate a simple particle with the appropriate number of points
    ParticlesDataMutable* simple=0;
    if(headersOnly){
        simple = new ParticleHeaders;
    }
    else{ 
        simple = create();
    }
    int numParticles = 0;
    input->read(tag, 4); // MYCH

    while(((int)input->tellg()-HEADER_SIZE) < blockSize){
        Attribute_Header attrHeader;
        ReadAttrHeader(*input, attrHeader);

        if(attrHeader.name == std::string("id")){
            numParticles = attrHeader.numParticles;
        }

        if(attrHeader.blocksize/sizeof(double) == 1){ // for who ?
            input->seekg((int)input->tellg() + attrHeader.blocksize);
            continue;
        }

        if(attrHeader.type == std::string("FVCA")){
            input->seekg((int)input->tellg() + attrHeader.blocksize);
            simple->addAttribute(attrHeader.name.c_str(), VECTOR, 3);
        }
        else if(attrHeader.type == std::string("DBLA")){
            input->seekg((int)input->tellg() + attrHeader.blocksize);
            simple->addAttribute(attrHeader.name.c_str(), FLOAT, 1);
        }
        else{
            input->seekg((int)input->tellg() + attrHeader.blocksize);
            std::cerr << "Partio: Attribute '" << attrHeader.name << " " << attrHeader.type << "' cannot map type" << std::endl;
        }
    }
    simple->addParticles(numParticles);
    
    // If all we care about is headers, then return.--
    if(headersOnly){
        return simple;
    }
    input->seekg(HEADER_SIZE);
    input->read(tag, 4); // MYCH
    while((int)input->tellg()-HEADER_SIZE < blockSize){
        Attribute_Header attrHeader;
        ReadAttrHeader(*input, attrHeader);

        if(attrHeader.blocksize/sizeof(double) == 1){ // for who ?
            input->seekg((int)input->tellg() + attrHeader.blocksize);
            continue;
        }

        ParticleAttribute attrHandle;
        if(simple->attributeInfo(attrHeader.name.c_str(), attrHandle) == false){
            input->seekg((int)input->tellg() + attrHeader.blocksize);
            continue;
        }

        Partio::ParticlesDataMutable::iterator it = simple->begin();
        Partio::ParticleAccessor accessor(attrHandle);
        it.addAccessor(accessor);

        if(attrHeader.type == std::string("DBLA")){
            for(int i = 0; i < simple->numParticles(); i++){
                double tmp;
                read<BIGEND>(*input, tmp);
                float* data = simple->dataWrite<float>(attrHandle, i);
                data[0] = (float)tmp;
            }
        }
        else if(attrHeader.type == std::string("FVCA")){
            for(Partio::ParticlesDataMutable::iterator end = simple->end(); it != end; ++it){
                input->read(accessor.raw<char>(it), sizeof(float)*attrHandle.count);
            }
            it = simple->begin();
            for(Partio::ParticlesDataMutable::iterator end = simple->end(); it != end; ++it){
                float* data = accessor.raw<float>(it);
                for(int i = 0; i < attrHandle.count; i++){
                    BIGEND::swap(data[i]);
                    //data[k]=buffer[attrOffsets[attrIndex]+k];
                    //data[i] = endianSwap<float>(data[i]);
                }
            }
            

        }
        else{
            std::cout << attrHeader.type << std::endl;
        }
    }
    return simple;
    
}

}
