#include "Solver.hpp"
#include "../utils/logger/Logger.hpp"
#include "Residual.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdlib.h>//pour la valeur absolue
#include <string>
#include <stdexcept>

Solver::Solver(Mesh* mesh, ees2d::io::InputParser* IC)
{
	Logger::getInstance()->AddLog("________________________Solver initialisation________________________",1);
    m_mesh = mesh;
	m_inputParameters = IC;

	Logger::getInstance()->AddLog("solving initial conditions...",1);

	m_Winf = new W();

	// double E = IC->m_Pressure / IC->m_Density / IC->m_Gamma + 0.5 * pow(IC->m_Mach, 2);//POURQUOI MACH ICI?
	// m_Winf->E = E;
	// m_Winf->H = E + IC->m_Pressure / IC->m_Density;
	// m_Winf->P = IC->m_Pressure;
	// m_Winf->rho = IC->m_Density;
	// m_Winf->u = cos(IC->m_aoa*M_PI/180)*IC->m_Mach;
	// m_Winf->v = sin(IC->m_aoa*M_PI/180)*IC->m_Mach;

	// Adimensionnel
	m_Vref = pow(m_inputParameters->m_gasConstant*m_inputParameters->m_Temp,0.5);
	m_Winf->rho = 1;
	m_Winf->P = 1;
	m_Winf->u = IC->m_Mach*pow(IC->m_Gamma,0.5)*cos(IC->m_aoa*M_PI/180);
	m_Winf->v = IC->m_Mach*pow(IC->m_Gamma,0.5)*sin(IC->m_aoa*M_PI/180);
	double velocity = pow((pow(m_Winf->u,2)+pow(m_Winf->v,2)),0.5);
	m_Winf->E = Solver::solveE(m_Winf->P, IC->m_Gamma, m_Winf->rho, m_Winf->u,m_Winf->v,0);
	m_Winf->H = m_Winf->E + m_Winf->P / m_Winf->rho;

	// initialise element2W
	m_element2W = new W[m_mesh->m_nElementTot];
	for(int iElement = 0;iElement<m_mesh->m_nElementTot;iElement++){
		m_element2W[iElement] = *m_Winf;
	}
	//m_element2W[13].P = 1.5;

	// initialise face2Fc
	m_face2Fc = new Fc[m_mesh->m_nFace];//Enlever? a voir

	// initialise residuals
	// m_element2Residuals.~Residual();
	m_element2Residuals = new Residual(m_mesh->m_nElement, m_inputParameters->m_maxIter);

	// initialise deltaW
	m_element2DeltaW = new DeltaW[m_mesh->m_nFace];


	// initialise markers
	Logger::getInstance()->AddLog("Defining the type of the border conditions face elements...",1);
	m_mesh->m_markers->Check4Face(m_mesh->m_face2Node, m_mesh->m_nFace);
	Logger::getInstance()->AddLog("Defining the type of the border conditions ghost cells...",1);
	m_mesh->m_markers->FindElements(m_mesh);

	// Chose Scheme
	std::string schemeName = IC->m_scheme;
	if(schemeName=="AVERAGE"){
		m_scheme = &Solver::ConvectiveFluxAverageScheme;
	} else if(schemeName=="ROE"){
		m_scheme = &Solver::ConvectiveFluxRoeScheme;
	} else
	{
		throw std::logic_error(schemeName + " scheme not implemented");
	}


	// Logger::getInstance()->AddLog("________________________Ghost cells initialisation____________________", 1);
	// Ajouter le code qui permet de update les Ghost cells


	Logger::getInstance()->AddLog("________________________Iterative Process Start_______________________", 1);
	float error = 1000;
	int iteration = 0;
	while (error>m_inputParameters->m_minResiudal && iteration<m_inputParameters->m_maxIter)
	{

		// Reset cond limit
		m_mesh->m_markers->Update(m_mesh, this);

		// On doit d'abord mettre les residus a 0 pour tous les elements!! au debut de chaque iteration
		m_element2Residuals->Reset();

		// Boucle sur les faces internes pour calculer les Flux
		for (int iFace = 0; iFace < m_mesh->m_nFaceNoBoundaries; iFace++) {
			// Rearrangement du vecteur normal a la face si necessaire
			Solver::DotProduct(iFace);

			// Calcul du flux convectif de la face iFace
			(this->*m_scheme)(iFace);

			// Mise a jour des Residus des elements connectes a iFace
			Solver::CalculateResiduals(iFace);
		}


		// Boucle sur les faces avec des conditions frontieres (BC Faces)
		for (int iFaceBC = m_mesh->m_nFaceNoBoundaries; iFaceBC < m_mesh->m_nFace; iFaceBC++) {
			// Rearrangement du vecteur normal a la face si necessaire
			Solver::DotProductBC(iFaceBC);

			// Calcul du flux convectif de la face externe
			(this->*m_scheme)(iFaceBC);
			//Solver::BCFlux(iFaceBC);//TODO doit modifier le if!


			// Mise a jour des Residus de l'element (singulier!) connecte a iFaceBC
			Solver::CalculateResidualsBC(iFaceBC);
		}


		// Boucle sur les elements une fois que les residuals sont tous calcules pour update W
		// La methode de Euler Explicite est utilisee
		for (int iElem = 0; iElem < m_mesh->m_nElement; iElem++) {
			// Calcul du pas de temps pour chaque element
			double LocalTimeStep = Solver::LocalTimeStep(iElem);
			Solver::CalculateDeltaW(iElem, LocalTimeStep);

			Solver::UpdateW(iElem);
		}

		iteration++;
		m_element2Residuals->solveRMS();
		error = m_element2Residuals->MaxRMS();
	}
	Logger::getInstance()->AddLog("Convergence after " + std::to_string(iteration) + " iterations.", 1);
	m_element2Residuals->Write2File(m_inputParameters->m_outputResidual);
}

Solver::~Solver()
{
	delete[] m_element2W;
	delete[] m_face2Fc;
	delete m_Winf;
	delete[] m_element2DeltaW;
	delete m_element2Residuals;
}


//__________________________________________________________________________
void Solver::SolveFc(){
	// while (residuals>minresiduals && iteration<MaxIteration)

	//Update --> uopdate les conditions limites


	for (int iFace = 0; iFace < m_mesh->m_nFace; iFace++) {
		// Calcul Fc
		(this->*m_scheme)(iFace);
	}

	// m_mesh->m_markers.update()
}

//__________________________________________________________________________
void Solver::ConvectiveFluxAverageScheme(int iFace) {
	// Schema avec les moyennes
	int elem1 = m_mesh->m_face2Element[2 * iFace + 0];
	int elem2 = m_mesh->m_face2Element[2 * iFace + 1];

	double rho_avg = 0.5 * (this->m_element2W[elem1].rho + this->m_element2W[elem2].rho);
	double u_avg = 0.5 * (this->m_element2W[elem1].u + this->m_element2W[elem2].u);
	double v_avg = 0.5 * (this->m_element2W[elem1].v + this->m_element2W[elem2].v);
	double P_avg = 0.5 * (this->m_element2W[elem1].P + this->m_element2W[elem2].P);
	double H_avg = 0.5 * (this->m_element2W[elem1].H + this->m_element2W[elem2].H);

	double nx = m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0];
	double ny = m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1];

	// Calcul de la vitesse contravariante
	double V = u_avg * nx + v_avg * ny;//TODO changer le 2 pour le cas 3D et mettre le nombre de noeuds de la face

	// Calcul de Fc
	this->m_face2Fc[iFace].rho = rho_avg * V;
	this->m_face2Fc[iFace].u = rho_avg * u_avg * V + nx * P_avg;
	this->m_face2Fc[iFace].v = rho_avg * v_avg * V + ny * P_avg;
	this->m_face2Fc[iFace].H = rho_avg * H_avg * V;
}

//__________________________________________________________________________
void Solver::ConvectiveFluxRoeScheme(int iFace) {
	// Schema de Roe

	int elem1 = m_mesh->m_face2Element[2 * iFace + 0];
	int elem2 = m_mesh->m_face2Element[2 * iFace + 1];

	// Pour faciliter la comprehension, calcul de sqrt(rho_L) et sqrt(rho_R)
	double sqrtRhoL = pow(this->m_element2W[elem1].rho, 0.5);
	double sqrtRhoR = pow(this->m_element2W[elem2].rho, 0.5);

	// 1. Calcul des Roe-averaged variables
	double rho_tilde = sqrtRhoL * sqrtRhoR;
	double u_tilde = (this->m_element2W[elem1].u * sqrtRhoL + this->m_element2W[elem2].u * sqrtRhoR) / (sqrtRhoL + sqrtRhoR);
	double v_tilde = (this->m_element2W[elem1].v * sqrtRhoL + this->m_element2W[elem2].v * sqrtRhoR) / (sqrtRhoL + sqrtRhoR);
	double H_tilde = (this->m_element2W[elem1].H * sqrtRhoL + this->m_element2W[elem2].H * sqrtRhoR) / (sqrtRhoL + sqrtRhoR);

	double q_tilde2 = pow(u_tilde, 2) + pow(v_tilde, 2);
	double c_tilde = pow((m_inputParameters->m_Gamma - 1) * (H_tilde - q_tilde2 / 2), 0.5);
	double V_tilde = u_tilde * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] + v_tilde * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1];

	// 2. Calcul des Delta
	double rhoDelta = (this->m_element2W[elem2].rho - this->m_element2W[elem1].rho);
	double uDelta = (this->m_element2W[elem2].u - this->m_element2W[elem1].u);
	double vDelta = (this->m_element2W[elem2].v - this->m_element2W[elem1].v);
	double pDelta = (this->m_element2W[elem2].P - this->m_element2W[elem1].P);
	double VDelta = ((this->m_element2W[elem2].u * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] + this->m_element2W[elem2].v * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1]) -
	                (this->m_element2W[elem1].u * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] + this->m_element2W[elem1].v * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1]));

	// 3. Calcul de Fc pour l'element de gauche (FcL), elem1
	// double V_FcL = this->m_element2W[elem1].u * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] + this->m_element2W[elem1].v * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1];

	// double rhoV_FcL = this->m_element2W[elem1].rho * V_FcL;
	// double rhouV_FcL = this->m_element2W[elem1].rho * this->m_element2W[elem1].u * V_FcL + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] * this->m_element2W[elem1].P;
	// double rhovV_FcL = this->m_element2W[elem1].rho * this->m_element2W[elem1].v * V_FcL + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1] * this->m_element2W[elem1].P;
	// double rhoHV_FcL = this->m_element2W[elem1].rho * this->m_element2W[elem1].H * V_FcL;


	// // 4. Calcul de Fc pour l'element de droite (FcR), elem2
	// double V_FcR = this->m_element2W[elem2].u * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] + this->m_element2W[elem2].v * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1];

	// double rhoV_FcR = this->m_element2W[elem2].rho * V_FcR;
	// double rhouV_FcR = this->m_element2W[elem2].rho * this->m_element2W[elem2].u * V_FcR + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] * this->m_element2W[elem2].P;
	// double rhovV_FcR = this->m_element2W[elem2].rho * this->m_element2W[elem2].v * V_FcR + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1] * this->m_element2W[elem2].P;
	// double rhoHV_FcR = this->m_element2W[elem2].rho * this->m_element2W[elem2].H * V_FcR;

	double u_centre, v_centre, rho_centre, H_centre, P_centre;

	u_centre = 0.5*(this->m_element2W[elem1].u+this->m_element2W[elem2].u);
	v_centre = 0.5*(this->m_element2W[elem1].v+this->m_element2W[elem2].v);
	rho_centre = 0.5*(this->m_element2W[elem1].rho+this->m_element2W[elem2].rho);
	H_centre = 0.5*(this->m_element2W[elem1].H+this->m_element2W[elem2].H);
	P_centre = 0.5*(this->m_element2W[elem1].P+this->m_element2W[elem2].P);

	double V_centre = u_centre * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] + v_centre * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1];

	double rhoV_centre = rho_centre * V_centre;
	double rhouV_centre = rho_centre * u_centre * V_centre + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] * P_centre;
	double rhovV_centre = rho_centre * v_centre * V_centre + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1] * P_centre;
	double rhoHV_centre = rho_centre * H_centre * V_centre;

	// 5. Calcul de la matrice de Roe
	// 5.01 Calul du facteur de correction de Hartens, delta

	double a_elem1 = pow(m_inputParameters->m_Gamma * this->m_element2W[elem1].P / this->m_element2W[elem1].rho, 0.5);//Vitesse du son de l'element 1
	// double a_elem2 = pow(m_inputParameters->m_Gamma * this->m_element2W[elem2].P / this->m_element2W[elem2].rho, 0.5);//Vitesse du son de l'element 2
	// double a_avg = (a_elem1 + a_elem2) / 2;                                                                           //Moyenne de la vitesse du son

	// double delta = 0.1 * a_avg;  //Premier cas possible
	//double delta = 0.1 * c_tilde;// Deuxieme cas possible, a voir lequel prendre

	double delta = (1.0 / 15.0) * a_elem1;

	// Pour DeltaFc1
	double HartensCorrectionF1;
	if (std::abs(V_tilde - c_tilde) > delta) {
		HartensCorrectionF1 = std::abs(V_tilde - c_tilde);
	} else {
		HartensCorrectionF1 = (pow(V_tilde - c_tilde, 2) + pow(delta, 2)) / (2 * delta);
	}

	// Pour DeltaFc5
	double HartensCorrectionF5;
	if (std::abs(V_tilde + c_tilde) > delta) {
		HartensCorrectionF5 = std::abs(V_tilde + c_tilde);
	} else {
		HartensCorrectionF5 = (pow(V_tilde + c_tilde, 2) + pow(delta, 2)) / (2 * delta);
	}

	// 5.1 Calcul de DeltaFc1
	double termFc1 = HartensCorrectionF1 * ((pDelta - rho_tilde * c_tilde * VDelta) / (2 * pow(c_tilde, 2)));

	double rhoV_Fc1 = termFc1;
	double rhouV_Fc1 = termFc1 * (u_tilde - c_tilde * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0]);
	double rhovV_Fc1 = termFc1 * (v_tilde - c_tilde * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1]);
	double rhoHV_Fc1 = termFc1 * (H_tilde - c_tilde * V_tilde);

	// 5.2 Calcul de DeltaFc234
	double termFc234 = std::abs(V_tilde) * (rhoDelta - pDelta / pow(c_tilde, 2));

	double rhoV_Fc234 = termFc234;
	double rhouV_Fc234 = termFc234 * u_tilde + std::abs(V_tilde) * rho_tilde * (uDelta - VDelta * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0]);
	double rhovV_Fc234 = termFc234 * v_tilde + std::abs(V_tilde) * rho_tilde * (vDelta - VDelta * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1]);
	double rhoHV_Fc234 = termFc234 * q_tilde2 / 2 + std::abs(V_tilde) * rho_tilde * (u_tilde * uDelta + v_tilde * vDelta - V_tilde * VDelta);

	// 5.2 Calcul de DeltaFc5
	double termFc5 = HartensCorrectionF5 * ((pDelta + rho_tilde * c_tilde * VDelta) / (2 * pow(c_tilde, 2)));

	double rhoV_Fc5 = termFc5;
	double rhouV_Fc5 = termFc5 * (u_tilde + c_tilde * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0]);
	double rhovV_Fc5 = termFc5 * (v_tilde + c_tilde * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1]);
	double rhoHV_Fc5 = termFc5 * (H_tilde + c_tilde * V_tilde);

	
	// 6. Calcul de Fc
	double rhoV_Fc, rhouV_Fc, rhovV_Fc, rhoHV_Fc;
	if(iFace<m_mesh->m_nFaceInt){
		rhoV_Fc = rhoV_centre + 0.5 * ( -rhoV_Fc1 - rhoV_Fc234 - rhoV_Fc5);
		rhouV_Fc = rhouV_centre + 0.5 * (- rhouV_Fc1 - rhouV_Fc234 - rhouV_Fc5);
		rhovV_Fc = rhovV_centre + 0.5 * (- rhovV_Fc1 - rhovV_Fc234 - rhovV_Fc5);
		rhoHV_Fc = rhoHV_centre + 0.5 * (- rhoHV_Fc1 - rhoHV_Fc234 - rhoHV_Fc5);
	} else{
		rhoV_Fc = rhoV_centre;
		rhouV_Fc =  rhouV_centre;
		rhovV_Fc =  rhovV_centre;
		rhoHV_Fc =  rhoHV_centre;
	}
	
	// 7. Met dans le pointeur
	this->m_face2Fc[iFace].rho = rhoV_Fc;
	this->m_face2Fc[iFace].u = rhouV_Fc;
	this->m_face2Fc[iFace].v = rhovV_Fc;
	this->m_face2Fc[iFace].H = rhoHV_Fc;
	if(std::isnan(this->m_face2Fc[iFace].rho)){
		int test = 1;
	}
}

/*
//__________________________________________________________________________
void Solver::BCFlux(int iFace) {
	int elem1 = m_mesh->m_face2Element[2 * iFace + 0];
	int elem2 = m_mesh->m_face2Element[2 * iFace + 1];

	// Vitesse contravariante (permet de determiner si inflow ou outflow)
	double V = this->m_element2W[elem1].u * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] + this->m_element2W[elem1].v * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1];

	// Calcul du nombre de Mach de l'element 1
	double a = pow(m_inputParameters->m_Gamma * this->m_element2W[elem1].P / this->m_element2W[elem1].rho, 0.5);
	double vitesse = pow(pow(this->m_element2W[elem1].u, 2) + pow(this->m_element2W[elem1].v, 2), 0.5);
	double Mach = vitesse / a;

	// wall BC flux
	if (Marker == "Wall") {//TODO change
		// Calcul des parametres
		double p = this->m_element2W[elem1].P;
		double u = this->m_element2W[elem1].u - (this->m_element2W[elem1].u * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] + this->m_element2W[elem1].v * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1]) * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0];
		double v = this->m_element2W[elem1].v - (this->m_element2W[elem1].u * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] + this->m_element2W[elem1].v * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1]) * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1];
		double rho = this->m_element2W[elem1].rho;

		// Mettre dans le vecteur Fc
		this->m_face2Fc[iFace].rho = 0;
		this->m_face2Fc[iFace].u = m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] * p;
		this->m_face2Fc[iFace].v = m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1] * p;
		this->m_face2Fc[iFace].H = 0;
	}

	else if (Marker == "Farfield") {//TODO change

		// Traitement des cellules outflow
		if (Mach >= 1 && V > 0) {//Supersonic Outflow
			double p = this->m_element2W[elem1].P;
			double u = this->m_element2W[elem1].u;
			double v = this->m_element2W[elem1].v;
			double rho = this->m_element2W[elem1].rho;

			double E = p / ((m_inputParameters->m_Gamma - 1) * rho) + 0.5 * (pow(u, 2) + pow(v, 2));
			double H = E + p / rho;

			// Mettre dans le vecteur Fc
			this->m_face2Fc[iFace].rho = rho * V;
			this->m_face2Fc[iFace].u = rho * u * V + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] * p;
			this->m_face2Fc[iFace].v = rho * v * V + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1] * p;
			this->m_face2Fc[iFace].H = rho * H * V;
		} else if (Mach < 1 && V > 0) {//Subsonic Outflow
			double p = m_Winf->P;
			double u = this->m_element2W[elem1].u + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] * (this->m_element2W[elem1].P - p) / (this->m_element2W[elem1].rho * a);
			double v = this->m_element2W[elem1].v + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1] * (this->m_element2W[elem1].P - p) / (this->m_element2W[elem1].rho * a);
			double rho = this->m_element2W[elem1].rho + (p - this->m_element2W[elem1].P) / (pow(a, 2));

			double E = p / ((m_inputParameters->m_Gamma - 1) * rho) + 0.5 * (pow(u, 2) + pow(v, 2));
			double H = E + p / rho;

			// Mettre dans le vecteur Fc
			this->m_face2Fc[iFace].rho = rho * V;
			this->m_face2Fc[iFace].u = rho * u * V + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] * p;
			this->m_face2Fc[iFace].v = rho * v * V + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1] * p;
			this->m_face2Fc[iFace].H = rho * H * V;
		}

		// Traitement des cellules inflow
		else if (Mach >= 1 && V <= 0) {//Supersonic Inflow
			double p = m_Winf->P;
			double u = m_Winf->u;
			double v = m_Winf->v;
			double rho = m_Winf->rho;

			double E = p / ((m_inputParameters->m_Gamma - 1) * rho) + 0.5 * (pow(u, 2) + pow(v, 2));
			double H = E + p / rho;

			// Mettre dans le vecteur Fc
			this->m_face2Fc[iFace].rho = rho * V;
			this->m_face2Fc[iFace].u = rho * u * V + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] * p;
			this->m_face2Fc[iFace].v = rho * v * V + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1] * p;
			this->m_face2Fc[iFace].H = rho * H * V;
		} else if (Mach < 1 && V <= 0) {//Subsonic Inflow
			double p = 0.5 * (m_Winf->P + this->m_element2W[elem1].P - this->m_element2W[elem1].rho * a * (m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] * (m_Winf->u - this->m_element2W[elem1].u) + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1] * (m_Winf->v - this->m_element2W[elem1].v)));
			double rho = m_Winf->rho + (p - m_Winf->P) / (pow(a, 2));
			double u = m_Winf->u - m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] * (m_Winf->P - p) / (this->m_element2W[elem1].rho * a);
			double v = m_Winf->v - m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1] * (m_Winf->P - p) / (this->m_element2W[elem1].rho * a);

			double E = p / ((m_inputParameters->m_Gamma - 1) * rho) + 0.5 * (pow(u, 2) + pow(v, 2));
			double H = E + p / rho;

			// Mettre dans le vecteur Fc
			this->m_face2Fc[iFace].rho = rho * V;
			this->m_face2Fc[iFace].u = rho * u * V + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] * p;
			this->m_face2Fc[iFace].v = rho * v * V + m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1] * p;
			this->m_face2Fc[iFace].H = rho * H * V;
		}
	}
}
*/
//__________________________________________________________________________
void Solver::DotProduct(int iFace) {
	int elem1 = m_mesh->m_face2Element[2 * iFace + 0];
	int elem2 = m_mesh->m_face2Element[2 * iFace + 1];

	//Calcul du produit scalaire pour savoir s'il faut changer le sens de la normale a la face
	double xElem1 = m_mesh->m_element2Center[m_mesh->m_nDime * elem1 + 0];
	double yElem1 = m_mesh->m_element2Center[m_mesh->m_nDime * elem1 + 1];

	double xElem2 = m_mesh->m_element2Center[m_mesh->m_nDime * elem2 + 0];
	double yElem2 = m_mesh->m_element2Center[m_mesh->m_nDime * elem2 + 1];

	double xVecteur = xElem2 - xElem1;  // vecteur allant de elem1 vers elem2
	double yVecteur = yElem2 - yElem1;

	double dotProduct = xVecteur * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] + yVecteur * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1];

	if (dotProduct < 0) {//TODO a verifier pour cette condition
		// Si on veut que la normale pointe vers l'element 1 (on va ensuite additionner le flux pour l'element 1 et soustraire le flux a l'element 2)
		m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] *= -1;
		m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1] *= -1;
	}
}

//__________________________________________________________________________
void Solver::DotProductBC(int iFace) {
	int elem1 = m_mesh->m_face2Element[2 * iFace + 0];
	int elem2 = m_mesh->m_face2Element[2 * iFace + 1];
	//Calcul du produit scalaire pour savoir s'il faut changer le sens de la normale a la face
	double xElem1 = m_mesh->m_element2Center[m_mesh->m_nDime * elem1 + 0];
	double yElem1 = m_mesh->m_element2Center[m_mesh->m_nDime * elem1 + 1];

	int node1 = m_mesh->m_face2Node[2 * iFace];
	double xnode1 = m_mesh->m_coor[m_mesh->m_nDime * node1 + 0];
	double ynode1 = m_mesh->m_coor[m_mesh->m_nDime * node1 + 1];

	double xVecteur = xnode1 - xElem1;
	double yVecteur = ynode1 - yElem1;

	double dotProduct = xVecteur * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] + yVecteur * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1];

	if (dotProduct < 0) {//TODO a verifier pour cette condition
		// Si on veut que la normale pointe vers l'element 1 (on va ensuite additionner le flux pour l'element 1 et soustraire le flux a l'element 2)
		m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] *= -1;
		m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1] *= -1;
	}
}

//__________________________________________________________________________
double Solver::LocalTimeStep(int iElem) {
	double CFL = m_inputParameters->m_cfl;
	// 1. Calcul des lambdas

	// 1.1 Calcul des Sum_Sx et Sum_Sy
	double Sum_Sx = 0;
	double Sum_Sy = 0;

	//On boucle sur les faces de l'element iElem
	int startI = m_mesh->m_element2FaceStart[iElem];
	int endI = m_mesh->m_element2FaceStart[iElem + 1];

	int iFace;
	for (int i = startI; i < endI; ++i) {
		iFace = m_mesh->m_element2Face[i];
		Sum_Sx += std::abs(m_mesh->m_face2FaceVector[m_mesh->m_nDime * iFace + 0]);
		Sum_Sy += std::abs(m_mesh->m_face2FaceVector[m_mesh->m_nDime * iFace + 1]);
	}

	// 1.2 Calcul des lambdas
	double lambda_Cx, lambda_Cy;
	double c = pow(m_inputParameters->m_Gamma * this->m_element2W[iElem].P / this->m_element2W[iElem].rho, 0.5);
	lambda_Cx = 0.5 * (std::abs(this->m_element2W[iElem].u) + c) * Sum_Sx;
	lambda_Cy = 0.5 * (std::abs(this->m_element2W[iElem].v) + c) * Sum_Sy;

	// 2. Calcul du TimeStep
	double LocalTimeStep = CFL * m_mesh->m_element2Volume[iElem] / (lambda_Cx + lambda_Cy);

	return LocalTimeStep;
}

//__________________________________________________________________________
void Solver::CalculateResiduals(int iFace) {
	int elem1 = m_mesh->m_face2Element[2 * iFace + 0];
	int elem2 = m_mesh->m_face2Element[2 * iFace + 1];
	double Area = m_mesh->m_face2Area[iFace];

	// verification that the vector is correctly alligned --------------------------------------------
	double xElem1 = m_mesh->m_element2Center[m_mesh->m_nDime * elem1 + 0];
	double yElem1 = m_mesh->m_element2Center[m_mesh->m_nDime * elem1 + 1];

	double xElem2 = m_mesh->m_element2Center[m_mesh->m_nDime * elem2 + 0];
	double yElem2 = m_mesh->m_element2Center[m_mesh->m_nDime * elem2 + 1];

	double xVecteur = xElem2 - xElem1;		// vector going from elem1 to elem2
	double yVecteur = yElem2 - yElem1;

	double dotProduct = xVecteur * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 0] + yVecteur * m_mesh->m_face2Normal[m_mesh->m_nDime * iFace + 1]; // normal should go from elem2 to elem1
	// if(dotProduct > 0){						// vector should go from elem2 to elem1
	// 	throw std::logic_error("incorrect orientation");
	// }
	// ------------------------------------------------------------------------------------------------
	*(m_element2Residuals->GetRho(elem1)) += this->m_face2Fc[iFace].rho * Area;
	*(m_element2Residuals->GetU(elem1)) += this->m_face2Fc[iFace].u * Area;
	*(m_element2Residuals->GetV(elem1)) += this->m_face2Fc[iFace].v * Area;
	*(m_element2Residuals->GetE(elem1)) += this->m_face2Fc[iFace].H * Area;

	*(m_element2Residuals->GetRho(elem2)) -= this->m_face2Fc[iFace].rho * Area;
	*(m_element2Residuals->GetU(elem2)) -= this->m_face2Fc[iFace].u * Area;
	*(m_element2Residuals->GetV(elem2)) -= this->m_face2Fc[iFace].v * Area;
	*(m_element2Residuals->GetE(elem2)) -= this->m_face2Fc[iFace].H * Area;
}

//__________________________________________________________________________
void Solver::CalculateResidualsBC(int iFace) {
	int elem1 = m_mesh->m_face2Element[2 * iFace + 0];
	double Area = m_mesh->m_face2Area[iFace];

	*(m_element2Residuals->GetRho(elem1)) += this->m_face2Fc[iFace].rho * Area;
	*(m_element2Residuals->GetU(elem1)) += this->m_face2Fc[iFace].u * Area;
	*(m_element2Residuals->GetV(elem1)) += this->m_face2Fc[iFace].v * Area;
	*(m_element2Residuals->GetE(elem1)) += this->m_face2Fc[iFace].H * Area;
	if(std::isnan(*(m_element2Residuals->GetRho(elem1)))){
		int test =1;
	}
}

//__________________________________________________________________________
void Solver::CalculateDeltaW(int iElem, double LocalTimeStep) {
	this->m_element2DeltaW[iElem].rho = -LocalTimeStep * *(m_element2Residuals->GetRho(iElem)) / m_mesh->m_element2Volume[iElem];
	this->m_element2DeltaW[iElem].u = -LocalTimeStep * *(m_element2Residuals->GetU(iElem)) / m_mesh->m_element2Volume[iElem];
	this->m_element2DeltaW[iElem].v = -LocalTimeStep * *(m_element2Residuals->GetV(iElem)) / m_mesh->m_element2Volume[iElem];
	this->m_element2DeltaW[iElem].E = -LocalTimeStep * *(m_element2Residuals->GetE(iElem)) / m_mesh->m_element2Volume[iElem];
	if(std::isnan(m_element2DeltaW[iElem].rho)){
		int test =1;
	}
}

//__________________________________________________________________________
void Solver::UpdateW(int iElem) {
	double rho =this->m_element2W[iElem].rho;
	this->m_element2W[iElem].rho += this->m_element2DeltaW[iElem].rho;
	this->m_element2W[iElem].u += this->m_element2DeltaW[iElem].u / this->m_element2W[iElem].rho;
	this->m_element2W[iElem].v += this->m_element2DeltaW[iElem].v / this->m_element2W[iElem].rho;
	this->m_element2W[iElem].E += this->m_element2DeltaW[iElem].E / this->m_element2W[iElem].rho;
	this->m_element2W[iElem].P = this->m_element2W[iElem].rho * (m_inputParameters->m_Gamma-1) * (this->m_element2W[iElem].E - 0.5 * (pow(this->m_element2W[iElem].u, 2) + pow(this->m_element2W[iElem].v, 2)));
	this->m_element2W[iElem].H = this->m_element2W[iElem].E + this->m_element2W[iElem].P / this->m_element2W[iElem].rho;
	if(std::isnan(m_element2W[iElem].rho)){
		int test =1;
	}
}

//__________________________________________________________________________
double Solver::solveE(double p, double gamma, double rho, double u, double v, double w){
	double E;
	E = p/(gamma-1)/rho+0.5*(pow(u,2)+pow(v,2)+pow(w,2));
	return E;
}
