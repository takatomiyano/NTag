#ifndef PMTHITCLUSTER_HH
#define PMTHITCLUSTER_HH

#include <functional>
#include <algorithm>

#include <skparmC.h>
#include <sktqC.h>

#include "PMTHit.hh"
#include "Cluster.hh"

class TTree;
class TQReal;

typedef struct OpeningAngleStats {
    float mean, median, stdev, skewness;
} OpeningAngleStats;

class PMTHitCluster : public Cluster<PMTHit>
{
    public:
        PMTHitCluster();
        PMTHitCluster(sktqz_common sktqz);
        PMTHitCluster(TQReal* tqreal, int flag=2/* default: in-gate */);

        void Append(const PMTHit& hit);
        void Append(const PMTHitCluster& hitCluster, bool inGateOnly=false);
        void AddTQReal(TQReal* tqreal, int flag=2/* default: in-gate */);

        void SetVertex(const TVector3& inVertex);
        inline const TVector3& GetVertex() const { return vertex; }
        bool HasVertex() { return bHasVertex; }
        void RemoveVertex();

        void Sort();

        void DumpAllElements() const { for (auto& hit: fElement) hit.Dump(); }
        
        void FillTQReal(TQReal* tqreal);

        const PMTHit& operator[] (int iHit) const { return fElement[iHit]; }

        PMTHitCluster Slice(int startIndex, Float tWidth);
        PMTHitCluster Slice(int startIndex, Float minusT, Float plusT);
        PMTHitCluster SliceRange(Float startT, Float minusT, Float plusT);
        PMTHitCluster SliceRange(Float minusT, Float plusT);

        unsigned int GetIndex(Float t);
        unsigned int GetLowerBoundIndex(Float t) { return std::lower_bound(fElement.begin(), fElement.end(), PMTHit(t, 0, 1, 1)) - fElement.begin();}
        unsigned int GetUpperBoundIndex(Float t) { return std::upper_bound(fElement.begin(), fElement.end(), PMTHit(t, 0, 1, 1)) - fElement.begin();}

        void ApplyDeadtime(Float deadtime);

        template<typename T>
        float Find(std::function<T(const PMTHit&)> projFunc,
                   std::function<T(const std::vector<T>&)> calcFunc)
        {
            return calcFunc(GetProjection(projFunc));
        }

        template<typename T>
        std::vector<T> GetProjection(std::function<T(const PMTHit&)> lambda)
        {
            std::vector<T> output;
            for_each(fElement.begin(), fElement.end(), [&](PMTHit hit){ output.push_back(lambda(hit)); });

            return output;
        }

        template<typename T>
        std::vector<T> operator[](std::function<T(const PMTHit&)> lambda) { return GetProjection(lambda); }

        PMTHit GetLastHit() { return fElement.back(); }

        std::array<float, 6> GetBetaArray();
        OpeningAngleStats GetOpeningAngleStats();
        TVector3 FindTRMSMinimizingVertex(float INITGRIDWIDTH=800, float MINGRIDWIDTH=50, float GRIDSHRINKRATE=0.5, float VTXSRCRANGE=5000);

    private:
        bool bSorted;
        bool bHasVertex;
        TVector3 vertex;

        void SetToF(bool unset=false);
};

PMTHitCluster operator+(const PMTHitCluster& hitCluster, const Float& time);
PMTHitCluster operator-(const PMTHitCluster& hitCluster, const Float& time);

#endif