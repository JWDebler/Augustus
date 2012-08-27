/**********************************************************************
 * file: compgenepred.cc licence: Artistic Licence, see file
 * LICENCE.TXT or
 * http://www.opensource.org/licenses/artistic-license.php descr.:
 * comparative gene prediction on multiple species authors: Mario
 * Stanke, Alexander Gebauer, Stefanie König
 *
 * date    |   author      |  changes
 * --------|---------------|------------------------------------------
 * 07.03.12| Mario Stanke  | creation of the file
 **********************************************************************/

#include "compgenepred.hh"
#include "orthograph.hh"
#include "mea.hh"
#include "genomicMSA.hh"
#include "geneMSA.hh"
#include "orthoexon.hh"
#include "namgene.hh"


CompGenePred::CompGenePred(){
    if (!Constant::speciesfilenames.empty()) {
        rsa = new MemSeqAccess();
    } else {
        rsa = new DbSeqAccess();
    }
}

void CompGenePred::start(){
    // read in alignment, determine orthologous sequence fragments


#ifdef DEBUG
    cout << "reading in the phylogenetic tree" << endl;
#endif
    PhyloTree *tree = new PhyloTree(Constant::treefile);  //has to be initialized before OrthoGraph

    OrthoGraph::tree = tree;
    GeneMSA::tree = tree;
    OrthoGraph::numSpecies = OrthoGraph::tree->species.size();


#ifdef DEBUG
    OrthoGraph::tree->printWithGraphviz("tree.dot");
#endif
  
    OrthoGraph::initOutputFiles();

    NAMGene namgene; // creates and initializes the states
    FeatureCollection extrinsicFeatures; // hints, empty for now, will later read in hints for sequence ranges from database
    SequenceFeatureCollection sfc(&extrinsicFeatures);
    StateModel::readAllParameters(); // read in the parameter files: species_{igenic,exon,intron,utr}_probs.pbl

    vector<string> speciesNames = OrthoGraph::tree->species;
    GenomicMSA msa;
    msa.readAlignment(speciesNames);
    msa.prepareExons();
    vector<int> offsets;

    // determine object that holds a sequence range for each species
    // loop over species
    GeneMSA::openOutputFiles();
    while (GeneMSA *geneRange = msa.getNextGene()) {
        OrthoGraph orthograph;
        for (int s = 0; s < speciesNames.size(); s++) {
            string seqID = geneRange->getSeqID(s);
            if (!seqID.empty()) {
                AnnoSequence *seqRange = rsa->getSeq(speciesNames[s], seqID, geneRange->getStart(s), geneRange->getEnd(s), geneRange->getStrand(s));
#ifdef DEBUG
                cout << "retrieving sequence:\t" << speciesNames[s] << ":" << geneRange->getSeqID(s) << "\t" << geneRange->getStart(s) << "-" << geneRange->getEnd(s) << "\t";
                if( geneRange->getStrand(s) == plusstrand )
                    cout << "+\t";
                else
                    cout << "-\t";
                cout << "(" <<geneRange->getEnd(s) - geneRange->getStart(s) + 1 << "bp)" << endl;
#endif
                orthograph.orthoSeqRanges[s] = seqRange;
                if (seqRange==NULL) {
                    cerr << "random sequence access failed on " << speciesNames[s] << ", " << geneRange->getSeqID(s) << ", " << geneRange->getStart(s) << ", " <<  geneRange->getEnd(s) << ", " << endl;
                    break;
                } else {
                    namgene.getPrepareModels(seqRange->sequence, seqRange->length); // is needed for IntronModel::dssProb in GenomicMSA::createExonCands
                    if (geneRange->getStrand(s)==plusstrand) {
                        offsets.push_back(geneRange->getStart(s));
                    } else {
                        offsets.push_back(geneRange->getSeqIDLength(s) - (geneRange->getEnd(s)) - 1);
                    }
                    geneRange->createExonCands(seqRange->sequence); // ToDo:make this reasonable after experience with the data
                    list<ExonCandidate*> additionalExons = *(geneRange->getExonCands(s));

                    namgene.doViterbiPiecewise(sfc, seqRange, bothstrands); // sampling
                    list<Gene> *alltranscripts = namgene.getAllTranscripts();
                    if(alltranscripts){
                        cout << "building Graph for " << speciesNames[s] << endl;
                        /* build datastructure for graph representation
                         * @stlist : list of all sampled states
                         */
                        list<Status> stlist;
                        if(!alltranscripts->empty()){
                            buildStatusList(alltranscripts, false, stlist);
                        }
                        //build graph
                        orthograph.graphs[s] = new SpeciesGraph(&stlist, orthograph.orthoSeqRanges[s]->length, additionalExons, speciesNames[s]);
                        orthograph.graphs[s]->buildGraph();

                        orthograph.ptrs_to_alltranscripts[s] = alltranscripts; //save pointers to transcripts and delete them after gene list is build
                    }
                }
            } else {
                offsets.push_back(0);
                geneRange->exoncands.push_back(NULL);
                geneRange->existingCandidates.push_back(NULL);
                cout<< speciesNames[s] << " doesn't exist in this part of the alignment."<< endl;
            }
        }
        geneRange->printGeneRanges();
        geneRange->printExonCands(offsets);
        geneRange->createOrthoExons(offsets);
        geneRange->printOrthoExons(offsets);
        orthograph.all_orthoex = geneRange->getOrthoExons();

        /*if(!orthograph.all_orthoex.empty()){
	    // iterative optimization of labelings in graphs
	    orthograph.optimize();
	    }*/

	// transfer max weight paths to genes + filter + ouput
	//orthograph.outputGenes();

        offsets.clear();
        delete geneRange;
    }

    GeneMSA::closeOutputFiles();
    OrthoGraph::closeOutputFiles();

    // free memory space of tree
    delete OrthoGraph::tree;

}
