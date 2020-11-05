#pragma once
#define SOLVER_H

#include"../mesh/Mesh.hpp"

class InitialConditions;

struct Fc
// Valeur storée dans les faces
{
    double rho;
    double u;
    double v;
	double P;
	double H;
};

struct W
// Valeur storée dans les éléments
{
	double rho;
	double u;
    double v;
	double P;
	double H;
};

class Solver
{
private:
    Mesh* m_mesh;
	Fc *m_face2Fc;
	W *m_element2W;

public:
    Solver(Mesh* mesh, InitialConditions* IC);
    ~Solver();

    void SolveFc();
	void ConvectiveFluxAverageScheme(int iFace);
	void ConvectiveFluxRoeScheme(int iFace);
};
