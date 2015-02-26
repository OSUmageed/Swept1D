#ifndef PDE_CLASSIC_H
#define PDE_CLASSIC_H

#include<vector>
#include<cstring>
#include<cassert>
#include<cstdlib>
#include<cmath>
#include<mpi.h>
#include"pde_common.h"
#include"PngWriter.hpp"

// ====== implementation of ClassicDiscretization1D ======= //

class ClassicDiscretization1D {
    private:
    int numGrids_;
    double dx_;
    double x0_;
    int numVariables_;
    double * variablesData_;

    char* pngFilename_;
    PngWriter png_;

    void commonInit_() {
        MPI_Init(0, 0);

        int iProc;
        MPI_Comm_rank(MPI_COMM_WORLD, &iProc);
        x0_ = numGrids_ * dx_ * iProc;
    }

    public:
    ClassicDiscretization1D()
    : numGrids_(100), dx_(1.), 
      variablesData_(0), pngFilename_(0), png_(0,0)
    {
        commonInit_();
    }

    virtual ~ClassicDiscretization1D() {
        if (variablesData_) {
            delete variablesData_;
        }
        MPI_Finalize();
    }

    private:
    template<size_t numVar>
    inline void applyInitialization_(
         void (&localOperator)(SpatialPoint<0, numVar>&),
         double *data, int iGrid)
    {
        double (*pData)[numVar] = (double (*)[numVar]) data;
        SpatialPoint<0, numVar> p(x0_ + iGrid * dx_, 0, pData[iGrid]);
        localOperator(p);
    }

    public:
    template<size_t numVar>
    ClassicDiscretization1D(int numGrids, double dx,
         void (&localOperator)(SpatialPoint<0, numVar>&))
    :
        numGrids_(numGrids), dx_(dx), numVariables_(numVar),
        pngFilename_(0), png_(0,0)
    {
        commonInit_();
        variablesData_ = new double[numVar * (numGrids_ + 2)];
    
        const int iGridLeft = 1, iGridRight = numGrids_;
        applyInitialization_(localOperator, variablesData_, iGridLeft);
        applyInitialization_(localOperator, variablesData_, iGridRight);

        ClassicSyncer1D sync(variablesData_, numGrids_, numVar);

        for (int iGrid = iGridLeft + 1; iGrid < iGridRight; ++iGrid) {
            applyInitialization_(localOperator, variablesData_, iGrid);
        }

        sync.waitTillDone();
    }

    private:
    template<size_t numInput, size_t numOutput>
    void applyLocalOp_(void (&localOperator)(SpatialPoint<numInput,numOutput>& local),
                 const double * input, double * output, int iGrid)
    {
        const double (*pInput)[numInput] = (const double(*)[numInput])input;
        double (*pOutput)[numOutput] = (double(*)[numOutput])output;

        SpatialPoint<numInput, numOutput> p(iGrid * dx_, pInput[iGrid], pOutput[iGrid]);
        SpatialPoint<numInput, numOutput> pL((iGrid-1) * dx_, pInput[iGrid-1], 0);
        SpatialPoint<numInput, numOutput> pR((iGrid+1) * dx_, pInput[iGrid+1], 0);
        p.addNeighbors(&pL, &pR);

        localOperator(p);
    }

    public:
    template<size_t numInput, size_t numOutput>
    void applyOp(void (&localOperator)(SpatialPoint<numInput,numOutput>& point))
    {
        assert(numInput == numVariables_);
        double * newVariablesData = new double[numOutput * (numGrids_ + 2)];

        const int iGridLeft = 1, iGridRight = numGrids_;

        applyLocalOp_(localOperator, variablesData_, newVariablesData, iGridLeft);
        applyLocalOp_(localOperator, variablesData_, newVariablesData, iGridRight);

        ClassicSyncer1D sync(newVariablesData, numGrids_, numOutput);

        for (int iGrid = iGridLeft + 1; iGrid < iGridRight; ++iGrid) {
            applyLocalOp_(localOperator, variablesData_, newVariablesData, iGrid);
        }

        delete[] variablesData_;
        variablesData_ = newVariablesData;
        numVariables_ = numOutput;

        sync.waitTillDone();
    }

    // ------------ write to png file ------------- //

    public:

    struct {
        class VarColor_ {
            private:
            int iVar_;
            double low_, high_;

            public:
            VarColor_() : iVar_(0), low_(0.0), high_(INFINITY) {}
            void set(int iVar, double low, double high) {
                iVar_ = iVar;
                low_ = low;
                high_ = high;
            }

            void assertIVarLessThan(int numVars) {
                assert(iVar_ < numVars);
            }

            double map(double* pVal) {
                return (pVal[iVar_] - low_) / (high_ - low_);
            }
        } red, green, blue;

        void assertIVarLessThan(int numVars) {
            red.assertIVarLessThan(numVars);
            green.assertIVarLessThan(numVars);
            blue.assertIVarLessThan(numVars);
        }
    } colorMap;

    void variablesToColor(int iStep)
    {
        for (int iGrid = 0; iGrid < numGrids_; ++iGrid) {
            colorMap.assertIVarLessThan(numVariables_);
            double* pGrid = variablesData_ + iGrid * numVariables_;
            double r = colorMap.red.map(pGrid),
                   g = colorMap.green.map(pGrid),
                   b = colorMap.blue.map(pGrid);
            png_.set(iGrid, iStep, r, g, b);
        }
    }

    void writePng() {
        if (pngFilename_) {
            png_.write(pngFilename_);
        }
    }

    void writePng(const char* filename) {
        if (pngFilename_) {
            delete[] pngFilename_;
        }

        pngFilename_ = new char[strlen(filename) + strlen(mpiRankString()) + 5];
        strcpy(pngFilename_, filename);
        strcat(pngFilename_, mpiRankString());
        strcat(pngFilename_, ".png");
        writePng();
    }
};

#endif
