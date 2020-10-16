#include "MeshReaderSU2.hpp"
#include "Parser.hpp"

#include <fstream>
#include <iostream>
#include <limits>// std::numeric_limits
#include <memory>//declarations of unique_ptr
#include <regex>
#include <sstream>
#include <sstream>// std::stringstream
#include <string>

MeshReaderSU2::MeshReaderSU2(std::string meshPath) {
	parser = new Parser(meshPath);
}

void MeshReaderSU2::ReadFile() {
	m_nDime = parser->ExtractNextInt();
	std::cout << "number of dimensions = " << m_nDime << std::endl;

	m_nNode = parser->ExtractNextInt();
	std::cout << "number of points = " << m_nNode << std::endl;

	MeshReaderSU2::ReadCoord();

	m_nElement = parser->ExtractNextInt();
	std::cout << "number of elements = " << m_nElement << std::endl;


	MeshReaderSU2::ReadConnect();

	// MeshReaderSU2::Read_marks();
	// std::cout << "finished marks" << std::endl;
}

void MeshReaderSU2::ReadCoord() {
	double val;

	// Creation of the coordinates array
	m_coor = std::make_unique<double[]>(m_nNode * m_nDime);

	// Filling of the coordinates array
	for (int inode = 0; inode < m_nNode; inode++) {
		for (int idim = 0; idim < m_nDime; idim++) {
			val = parser->ExtractNextDouble();
			m_coor[inode * m_nDime + idim] = val;
			//std::cout << val << std::endl;
		}
	}
}

int MeshReaderSU2::VtkElem2NNode(int VTKid) {
	int number_of_nodes;
	switch (VTKid) {
		case 3:
			number_of_nodes = 2;
			break;

		case 5:
			number_of_nodes = 3;
			break;

		case 9:
			number_of_nodes = 4;
			break;

		case 10:
			number_of_nodes = 4;
			break;

		case 12:
			number_of_nodes = 8;
			break;

		case 13:
			number_of_nodes = 6;
			break;

		case 14:
			number_of_nodes = 5;
			break;
	}

	return number_of_nodes;
}

void MeshReaderSU2::ReadConnect() {
	int val;
	int filePosition = parser->m_inFile.tellg();
	int element2NodeSize = 0;
	int nNode;

	// Counting size of connectivity matrix
	for (int ielem = 0; ielem < m_nElement; ielem++) {
		val = parser->ExtractNextInt();
		nNode = MeshReaderSU2::VtkElem2NNode(val);
		for (int inode = 0; inode < nNode; inode++) {
			parser->GetNextWord();
			element2NodeSize++;
		}
	}
	parser->m_inFile.seekg(filePosition);


	// Filling of the connectivity matrix
	m_element2Node = std::make_unique<int[]>(element2NodeSize);
	m_element2NodeStart = std::make_unique<int[]>(m_nElement);

	int element2NodeCurrentStart = 0;

	for (int ielem = 0; ielem < m_nElement; ielem++) {
		m_element2NodeStart[ielem] = element2NodeCurrentStart;

		val = parser->ExtractNextInt();
		nNode = MeshReaderSU2::VtkElem2NNode(val);

		for (int iNode = 0; iNode < nNode; iNode++) {
			val = parser->ExtractNextInt();
			m_element2Node[element2NodeCurrentStart] = val;
			element2NodeCurrentStart++;
		}
	}
}

void MeshReaderSU2::ReadMarks() {
	// nmark = MeshReader::Read_int();
	// std::cout << "NMARK = " << nmark << std::endl;
}