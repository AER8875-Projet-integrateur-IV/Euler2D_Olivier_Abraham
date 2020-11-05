#include "mesh/generator/MeshReader.hpp"
#include "mesh/generator/MeshReaderSU2.hpp"
#include "mesh/generator/Parser.hpp"
#include "mesh/generator/MeshGenerator.hpp"
#include "mesh/Mesh.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include "utils/logger/Logger.hpp"
#include "mesh/metrics/MetricsGenerator.hpp"
#include "InputParser.h"

void show_usage() {
	std::cerr << "Usage: "
	          << "Options:\n"
	          << "\t-h,--help\t\tShow this help message\n"
	          << "\t-i,--input\tSpecify the file path for the EES2D Software Input file\n"
			  << "\t-v,--verbose\tOutput debugging information\n"
			  << "\t-vv,--veryverbose\tOutput EVERYTHING !?!?!?!?\n"
	          << std::endl;
}

int main(int argc, char *argv[]) {

	std::cout << "Main is running" << std::endl;

	std::string inpath;
	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		if ((arg == "-h") || (arg == "--help")) {
			show_usage();
			return 0;
		} else if ((arg == "-i") || (arg == "--input")) {
			inpath = argv[++i];
			Logger::getInstance()->AddLog("input mesh path = " + inpath,0);
		} else if ((arg == "-v") || (arg == "--verbose")) {
			argv[++i];
			Logger::getInstance()->SetVerbosity(1);
		} else if ((arg == "-vv") || (arg == "--veryverbose")) {
			argv[++i];
			Logger::getInstance()->SetVerbosity(2);
		}
	}

	ees2d::io::InputParser inputParameters{inpath};
	inputParameters.parse();

	Mesh mesh = Mesh();

	MeshReaderSU2 reader(inputParameters.m_meshFile, &mesh); 
	reader.ReadFile();

	MeshGenerator generator(&mesh);
	generator.BuildMesh();

	MetricsGenerator metrics(&mesh);
	metrics.Solve();
	
	printf("nFace = %2d \n",mesh.m_nFace);
	for (int i=0;i<mesh.m_nFace*2;i++){
		printf("%2d ",mesh.m_face2Element[i]);
		// std::cout<<to_string()<< " " <<std::endl;
	}
	
	for (int i=0;i<mesh.m_nElement*2;i++){
		//printf("%f ",metrics.m_mesh->m_element2Volume[i]);
		//printf("\n");
		//printf("%f ",metrics.m_mesh->m_element[i]);
		//printf("\n");
		//printf("%f ",metrics.m_mesh->m_element2Volume[i]);
		//printf("\n");
		// std::cout<<to_string()<< " " <<std::endl;
	}


	return 0;
}