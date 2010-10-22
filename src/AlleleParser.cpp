#include "AlleleParser.h"
#include "multichoose.h" // includes generic functions, so it must be included here
                         // otherwise we will get a linker error
                         // see: http://stackoverflow.com/questions/36039/templates-spread-across-multiple-files
                         // http://www.cplusplus.com/doc/tutorial/templates/ "Templates and Multi-file projects"
#include "multipermute.h"

// local helper debugging macros to improve code readability
#define DEBUG(msg) \
    if (parameters.debug) { cerr << msg << endl; }

// lower-priority messages
#ifdef VERBOSE_DEBUG
#define DEBUG2(msg) \
    if (parameters.debug2) { cerr << msg << endl; }
#else
#define DEBUG2(msg)
#endif

// must-see error messages
#define ERROR(msg) \
    cerr << msg << endl;

using namespace std;


// XXX TODO change these void functions to bool

// open BAM input file
void AlleleParser::openBams(void) {

    // report differently if we have one or many bam files
    if (parameters.bams.size() == 1) {
        DEBUG("Opening BAM fomat alignment input file: " << parameters.bams.front() << " ...");
    } else if (parameters.bams.size() > 1) {
        DEBUG("Opening " << parameters.bams.size() << " BAM fomat alignment input files");
        for (vector<string>::const_iterator b = parameters.bams.begin(); 
                b != parameters.bams.end(); ++b) {
            DEBUG2(*b);
        }
    }
    
    // set no index caching if we are only making one jump
    if (targets.size() == 1) {
        bamMultiReader.SetIndexCacheMode(BamToolsIndex::NoIndexCaching);
    }

    if ( !bamMultiReader.Open(parameters.bams, true) ) {
        if ( !bamMultiReader.Open(parameters.bams, false) ) {
            ERROR("Could not open input BAM files");
            exit(1);
        } else {
            ERROR("Opened BAM reader without index file, jumping is disabled.");
            // TODO set a flag, check if there are targets specified?
            if (targets.size() > 0) {
                ERROR("Targets specified but no BAM index file provided.");
                ERROR("FreeBayes cannot jump through targets in BAM files without BAM index files, exiting.");
                ERROR("Please generate a BAM index file either .bai (standard) or .bti (bamtools), e.g.:");
                ERROR("bamtools index -in <bam_file >");
                exit(1);
            }
        }
    }
    DEBUG(" done");

}

void AlleleParser::openTraceFile(void) {
    if (parameters.trace) {
        traceFile.open(parameters.traceFile.c_str(), ios::out);
        DEBUG("Opening trace file: " << parameters.traceFile << " ...");
        if (!traceFile) {
            ERROR(" unable to open trace file: " << parameters.traceFile );
            exit(1);
        }
    }
}

void AlleleParser::openOutputFile(void) {
    if (parameters.outputFile != "") {
        outputFile.open(parameters.outputFile.c_str(), ios::out);
        DEBUG("Opening output file: " << parameters.outputFile << " ...");
        if (!outputFile) {
            ERROR(" unable to open output file: " << parameters.outputFile);
            exit(1);
        }
        output = &outputFile;
    } else {
        output = &cout;
    }
}

// read sample list file or get sample names from bam file header
void AlleleParser::getSampleNames(void) {

    // If a sample file is given, use it.  But otherwise process the bam file
    // header to get the sample names.
    //

    if (parameters.samples != "") {
        ifstream sampleFile(parameters.samples.c_str(), ios::in);
        if (! sampleFile) {
            cerr << "unable to open file: " << parameters.samples << endl;
            exit(1);
        }
        string line;
        while (getline(sampleFile, line)) {
            DEBUG2("found sample " << line);
            sampleList.push_back(line);
        }
    }

    // retrieve header information

    string bamHeader = bamMultiReader.GetHeaderText();

    vector<string> headerLines = split(bamHeader, '\n');

    for (vector<string>::const_iterator it = headerLines.begin(); it != headerLines.end(); ++it) {

        // get next line from header, skip if empty
        string headerLine = *it;
        if ( headerLine.empty() ) { continue; }

        // lines of the header look like:
        // "@RG     ID:-    SM:NA11832      CN:BCM  PL:454"
        //                     ^^^^^^^\ is our sample name
        if ( headerLine.find("@RG") == 0 ) {
            vector<string> readGroupParts = split(headerLine, "\t ");
            string name = "";
            string readGroupID = "";
            for (vector<string>::const_iterator r = readGroupParts.begin(); r != readGroupParts.end(); ++r) {
                vector<string> nameParts = split(*r, ":");
                if (nameParts.at(0) == "SM") {
                   name = nameParts.at(1);
                } else if (nameParts.at(0) == "ID") {
                   readGroupID = nameParts.at(1);
                }
            }
            if (name == "") {
                ERROR(" could not find SM: in @RG tag " << endl << headerLine);
                exit(1);
            }
            if (readGroupID == "") {
                ERROR(" could not find ID: in @RG tag " << endl << headerLine);
                exit(1);
            }
            //string name = nameParts.back();
            //mergedHeader.append(1, '\n');
            DEBUG2("found read group id " << readGroupID << " containing sample " << name);
            sampleListFromBam.push_back(name);
            readGroupToSampleNames[readGroupID] = name;
        }
    }
    //cout << sampleListFromBam.size() << endl;
     // no samples file given, read from BAM file header for sample names
    if (sampleList.size() == 0) {
        DEBUG("no sample list file given, reading sample names from bam file");
        for (vector<string>::const_iterator s = sampleListFromBam.begin(); s != sampleListFromBam.end(); ++s) {
            DEBUG2("found sample " << *s);
            if (!stringInVector(*s, sampleList)) {
                sampleList.push_back(*s);
            }
        }
        DEBUG("found " << sampleList.size() << " samples");
    } else {
        // verify that the samples in the sample list are present in the bam,
        // and raise an error and exit if not
        for (vector<string>::const_iterator s = sampleList.begin(); s != sampleList.end(); ++s) {
            bool inBam = false;
            bool inReadGroup = false;
            //cout << "checking sample from sample file " << *s << endl;
            for (vector<string>::const_iterator b = sampleListFromBam.begin(); b != sampleListFromBam.end(); ++b) {
                //cout << *s << " against " << *b << endl;
                if (*s == *b) { inBam = true; break; }
            }
            for (map<string, string>::const_iterator p = readGroupToSampleNames.begin(); p != readGroupToSampleNames.end(); ++p) {
                if (*s == p->second) { inReadGroup = true; break; }
            }
            if (!inBam) {
                ERROR("sample " << *s << " listed in sample file "
                    << parameters.samples.c_str() << " is not listed in the header of BAM file(s) "
                    << parameters.bam);
                exit(1);
            }
            if (!inReadGroup) {
                ERROR("sample " << *s << " listed in sample file "
                    << parameters.samples.c_str() << " is not associated with any read group in the header of BAM file(s) "
                    << parameters.bam);
                exit(1);
            }

        }
    }

    if (sampleList.size() == 0) {
        ERROR("No sample names given, and no @RG tags found in BAM file(s).");
        exit(1);
    }
}

void AlleleParser::writeVcfHeader(ostream& out) {

    time_t rawtime;
    struct tm * timeinfo;
    char datestr [80];

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(datestr, 80, "%Y%m%d %X", timeinfo);

    out << "##format=VCFv4.0" << endl
            << "##fileDate=" << datestr << endl
            << "##source=bambayes" << endl
            << "##reference=" << parameters.fasta << endl
            << "##phasing=none" << endl
            << "##notes=\"All FORMAT fields matching *i* (e.g. NiBAll, NiA) refer to individuals.\"" << endl

            << "##INFO=NS,1,Integer,\"total number of samples\"" << endl
            << "##INFO=ND,1,Integer,\"total number of non-duplicate samples\"" << endl
            << "##INFO=DP,1,Integer,\"total read depth at this base\"" << endl
            << "##INFO=AC,1,Integer,\"total number of alternate alleles in called genotypes\"" << endl
            //<< "##INFO=AN,1,Integer,\"total number of alleles in called genotypes\"" << endl

            // these are req'd
            << "##FORMAT=GT,1,String,\"Genotype\"" << endl // g
            << "##FORMAT=GQ,1,Integer,\"Genotype Quality\"" << endl // phred prob of genotype
            << "##FORMAT=DP,1,Integer,\"Read Depth\"" << endl // NiBAll[ind]
            << "##FORMAT=HQ,2,Integer,\"Haplotype Quality\"" << endl
            << "##FORMAT=QiB,1,Integer,\"Total base quality\"" << endl
            << "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\t"
            << join(sampleList, "\t")
            << endl;

}

void AlleleParser::loadBamReferenceSequenceNames(void) {

    //--------------------------------------------------------------------------
    // read reference sequences from input file
    //--------------------------------------------------------------------------

    // store the names of all the reference sequences in the BAM file
    referenceSequences = bamMultiReader.GetReferenceData();

    DEBUG("Number of ref seqs: " << bamMultiReader.GetReferenceCount());

}


void AlleleParser::loadFastaReference(void) {

    DEBUG("loading fasta reference " << parameters.fasta);

    // This call loads the reference and reads any index file it can find.  If
    // it can't find an index file for the reference, it will attempt to
    // generate one alongside it.  Note that this only loads the reference.
    // Sequence data is obtained by progressive calls to
    // reference->getSubSequence(..), thus keeping our memory requirements low.

    reference = new FastaReference(parameters.fasta);

}

// intended to load all the sequence covered by reads which overlap our current target
// this lets us process the reads fully, checking for suspicious reads, etc.
// but does not require us to load the whole sequence
void AlleleParser::loadReferenceSequence(BedTarget* target, int before, int after) {
    basesBeforeCurrentTarget = before;
    basesAfterCurrentTarget = after;
    DEBUG2("loading reference subsequence " << target->seq << " from " << target->left << " - " << before << " to " << target->right << " + " << after << " + before");
    string name = reference->sequenceNameStartingWith(target->seq);
    currentSequence = reference->getSubSequence(name, (target->left - 1) - before, (target->right - target->left) + after + before);
    //loadReferenceSequence(name, target->left - 1 - before, target->right - target->left + after + before);
}

// used to extend the cached reference subsequence when we encounter a read which extends beyond its right bound
void AlleleParser::extendReferenceSequence(int rightExtension) {
    DEBUG2("extending reference subsequence right by " << rightExtension << " bp");
    currentSequence += reference->getSubSequence(reference->sequenceNameStartingWith(currentTarget->seq), 
                                                 (currentTarget->right - 1) + basesAfterCurrentTarget,
                                                 rightExtension);
    basesAfterCurrentTarget += rightExtension;
}

void AlleleParser::loadTargets(void) {

    // if we have a region specified, use it to generate a target
    if (parameters.region != "") {
        // drawn from bamtools_utilities.cpp, modified to suit 1-based context, no end sequence

        string region = parameters.region;
        string startSeq;
        int startPos;
        int stopPos;

        size_t foundFirstColon = region.find(":");

        // we only have a single string, use the whole sequence as the target
        if (foundFirstColon == string::npos) {
            startSeq = region;
            startPos = 1;
            stopPos = -1;
        } else {
            startSeq = region.substr(0, foundFirstColon);
            size_t foundRangeDots = region.find("..", foundFirstColon);
            if (foundRangeDots == string::npos) {
                startPos = atoi(region.substr(foundFirstColon + 1).c_str());
                // differ from bamtools in this regard, in that we process only
                // the specified position if a range isn't given
                stopPos = startPos + 1;
            } else {
                startPos = atoi(region.substr(foundFirstColon + 1, foundRangeDots - foundRangeDots - 1).c_str());
                stopPos = atoi(region.substr(foundRangeDots + 2).c_str()); // to the start of this chromosome
            }
        }

        BedTarget bd(startSeq,
                    (startPos == 1) ? 1 : startPos,
                    (stopPos == -1) ? reference->sequenceLength(startSeq) : stopPos);
        DEBUG2("will process reference sequence " << startSeq << ":" << bd.left << ".." << bd.right);
        targets.push_back(bd);
    }

    // if we have a targets file, use it...
    // if target file specified use targets from file
    if (parameters.targets != "") {

        DEBUG("Making BedReader object for target file: " << parameters.targets << " ...");

        BedReader bedReader(parameters.targets);

        if (!bedReader.is_open()) {
            ERROR("Unable to open target file: " << parameters.targets << "... terminating.");
            exit(1);
        }

        targets = bedReader.entries();

        // check validity of targets wrt. reference
        for (vector<BedTarget>::iterator e = targets.begin(); e != targets.end(); ++e) {
            BedTarget& bd = *e;
            if (bd.left < 1 || bd.right < bd.left || bd.right >= reference->sequenceLength(bd.seq)) {
                ERROR("Target region coordinates (" << bd.seq << " "
                        << bd.left << " " << bd.right
                        << ") outside of reference sequence bounds ("
                        << bd.seq << " " << reference->sequenceLength(bd.seq) << ") terminating.");
                exit(1);
            }
        }

        if (targets.size() == 0) {
            ERROR("Could not load any targets from " << parameters.targets);
            exit(1);
        }

        bedReader.close();

        DEBUG("done");

    }

    DEBUG("Number of target regions: " << targets.size());

}

void AlleleParser::loadTargetsFromBams(void) {
    // otherwise, if we weren't given a region string or targets file, analyze
    // all reference sequences from BAM file
    DEBUG2("no targets specified, using all targets from BAM files");
    RefVector::iterator refIter = referenceSequences.begin();
    RefVector::iterator refEnd  = referenceSequences.end();
    for( ; refIter != refEnd; ++refIter) {
        RefData refData = *refIter;
        string refName = refData.RefName;
        BedTarget bd(refName, 1, refData.RefLength);
        DEBUG2("will process reference sequence " << refName << ":" << bd.left << ".." << bd.right);
        targets.push_back(bd);
    }
}

// initialization function
// sets up environment so we can start registering alleles
AlleleParser::AlleleParser(int argc, char** argv) : parameters(Parameters(argc, argv))
{

    // initialization
    openTraceFile();
    openOutputFile();

    // check how many targets we have specified
    loadTargets();
    // when we open the bam files we can use the number of targets to decide if
    // we should load the indexes
    openBams();
    loadBamReferenceSequenceNames();
    loadFastaReference();
    getSampleNames();

    // if we don't have any targets specified, now use the BAM header to get
    // the targets to analyze
    if (targets.size() == 0)
        loadTargetsFromBams();

    currentRefID = 0; // will get set properly via toNextRefID
    currentTarget = NULL; // to be initialized on first call to getNextAlleles
    currentReferenceAllele = NULL; // same, NULL is brazenly used as an initialization flag
    justSwitchedTargets = false;  // flag to trigger cleanup of Allele*'s and objects after jumping targets

}

AlleleParser::~AlleleParser(void) {
    // close trace file?  seems to get closed properly on object deletion...
    if (currentReferenceAllele != NULL) delete currentReferenceAllele;
    delete reference;
}

// position of alignment relative to current sequence
int AlleleParser::currentSequencePosition(const BamAlignment& alignment) {
    return (alignment.Position - (currentTarget->left - 1)) + basesBeforeCurrentTarget;
}

// TODO clean up this.... just use a string
char AlleleParser::currentReferenceBaseChar(void) {
    return *currentReferenceBaseIterator();
}

string AlleleParser::currentReferenceBaseString(void) {
    return currentSequence.substr((currentPosition - (currentTarget->left - 1)) + basesBeforeCurrentTarget, 1);
}

string::iterator AlleleParser::currentReferenceBaseIterator(void) {
    return currentSequence.begin() + (currentPosition - (currentTarget->left - 1)) + basesBeforeCurrentTarget;
}

// registeredalignment friend
ostream& operator<<(ostream& out, RegisteredAlignment& ra) {
    out << ra.alignment.Name << " " << ra.alignment.Position << endl
        << ra.alignment.QueryBases << endl
        << ra.alignment.Qualities << endl
        << ra.alleles << endl;
    return out;
}

RegisteredAlignment AlleleParser::registerAlignment(BamAlignment& alignment, string sampleName) {

    RegisteredAlignment ra = RegisteredAlignment(alignment); // result

    string rDna = alignment.QueryBases;
    string rQual = alignment.Qualities;
    int rp = 0;  // read position, 0-based relative to read
    int csp = currentSequencePosition(alignment); // current sequence position, 0-based relative to currentSequence
    int sp = alignment.Position;  // sequence position

    string readName = alignment.Name;

#ifdef VERBOSE_DEBUG
    if (parameters.debug2) {
        DEBUG2("registering alignment " << rp << " " << csp << " " << sp << endl <<
                "alignment readName " << readName << endl <<
                "alignment isPaired " << alignment.IsPaired() << endl <<
                "alignment sampleID " << sampleName << endl << 
                "alignment position " << alignment.Position << endl <<
                "alignment length " << alignment.Length << endl <<
                "alignment AlignedBases.size() " << alignment.AlignedBases.size() << endl <<
                "alignment end position " << alignment.Position + alignment.AlignedBases.size());

        int alignedLength = 0;
        for (vector<CigarOp>::const_iterator c = alignment.CigarData.begin(); c != alignment.CigarData.end(); ++c) {
            if (c->Type == 'D')
                alignedLength += c->Length;
            if (c->Type == 'M')
                alignedLength += c->Length;
        }

        DEBUG2(rDna << endl << alignment.AlignedBases << endl << currentSequence.substr(csp, alignedLength));
    }
#endif

    /*
     * This should be simpler, but isn't because the cigar only records matches
     * for sequences that have embedded mismatches.
     *
     * Also, we don't store the entire undelying sequence; just the subsequence
     * that matches our current target region.
     * 
     * As we step through a match sequence, we look for mismatches.  When we
     * see one we set a positional flag indicating the location, and we emit a
     * 'Reference' allele that stretches from the the base after the last
     * mismatch to the base before the current one.
     *
     * An example follows:
     *
     * NNNNNNNNNNNMNNNNNNNNNNNNNNNN
     * reference  ^\-snp  reference
     *
     */

    vector<bool> indelMask (alignment.AlignedBases.size(), false);

    vector<CigarOp>::const_iterator cigarIter = alignment.CigarData.begin();
    vector<CigarOp>::const_iterator cigarEnd  = alignment.CigarData.end();
    for ( ; cigarIter != cigarEnd; ++cigarIter ) {
        unsigned int l = cigarIter->Length;
        char t = cigarIter->Type;
        DEBUG2("cigar item: " << t << l);

        if (t == 'M') { // match or mismatch
            int firstMatch = csp; // track the first match after a mismatch, for recording 'reference' alleles
            //cerr << "firstMatch = " << firstMatch << endl;
                                        // we start one back because the first position in a match should match...

            // for each base in the match region
            // increment the csp, sp, and rp
            // if there is a mismatch, record the last matching stretch as a reference allele
            // presently just record one snp per mismatched position, whether or not they are in a series

            for (int i=0; i<l; i++) {

                // extract aligned base
                string b;
                TRY { b = rDna.at(rp); } CATCH;

                // convert base quality value into short int
                short qual = qualityChar2ShortInt(rQual.at(rp));

                // get reference allele
                string sb;
                TRY { sb = currentSequence.at(csp); } CATCH;

                // XXX XXX XXX THIS DEBUGIC IS ILDEBUGICAL!!!
                //
                // record mismatch if we have a mismatch here
                if (b != sb) {
                    if (firstMatch < csp) {
                        // TODO ; verify that the read and reference sequences *do* match
                        int length = csp - firstMatch;
                        string matchingSequence = currentSequence.substr(csp - length, length);
                        //cerr << "matchingSequence " << matchingSequence << endl;
                        string readSequence = rDna.substr(rp - length, length);
                        //cerr << "readSequence " << readSequence << endl;
                        string qualstr = rQual.substr(rp - length, length);
                        // record 'reference' allele for last matching region
                        Allele* allele = new Allele(ALLELE_REFERENCE,
                                currentTarget->seq, sp - length, &currentPosition, &currentReferenceBase, length, 
                                matchingSequence, readSequence, sampleName, alignment.Name,
                                !alignment.IsReverseStrand(), -1, qualstr,
                                alignment.MapQuality);
                        DEBUG2(allele);
                        ra.alleles.push_back(allele);
                    }
                    // register mismatch
                    if (qual >= parameters.BQL2) {
                        ra.mismatches++;  // increment our mismatch counter if we're over BQL2
                    }
                    // always emit a snp, if we have too many mismatches over
                    // BQL2 then we will discard the registered allele in the
                    // calling context
                    Allele* allele = new Allele(ALLELE_SNP, currentTarget->seq, sp, &currentPosition, &currentReferenceBase, 1, sb, b,
                            sampleName, alignment.Name, !alignment.IsReverseStrand(), qual, "", alignment.MapQuality);
                    DEBUG2(allele);
                    ra.alleles.push_back(allele);
                    firstMatch = csp + 1;
                }

                // update positions
                ++sp;
                ++csp;
                ++rp;
            }
            if (firstMatch < csp) {
                int length = csp - firstMatch;
                string matchingSequence = currentSequence.substr(csp - length, length);
                string readSequence = rDna.substr(rp - length, length);
                string qualstr = rQual.substr(rp - length, length);
                Allele* allele = new Allele(ALLELE_REFERENCE,
                        currentTarget->seq, sp - length, &currentPosition, &currentReferenceBase, length, 
                        matchingSequence, readSequence, sampleName, alignment.Name,
                        !alignment.IsReverseStrand(), -1, qualstr,
                        alignment.MapQuality);
                DEBUG2(allele);
                ra.alleles.push_back(allele);
            }
        } else if (t == 'D') { // deletion

            // extract base quality of left and right flanking non-deleted bases
            string qualstr = rQual.substr(rp, 2);

            // calculate joint quality of the two flanking qualities
            short qual = max(qualityChar2ShortInt(qualstr[0]), qualityChar2ShortInt(qualstr[1]));
            if (qual >= parameters.BQL2) {
                ra.mismatches += l;
            }
            // XXX indel window exclusion
            // XXX this only excludes after the deletion
            //if (qual >= parameters.BQL0) {
            if (qual >= parameters.BQL2) {
                for (int i=0; i<l; i++) {
                    indelMask[sp - alignment.Position + i] = true;
                }
            }
            //}
            Allele* allele = new Allele(ALLELE_DELETION,
                    currentTarget->seq, sp, &currentPosition, &currentReferenceBase, l,
                    currentSequence.substr(csp, l), "", sampleName, alignment.Name,
                    !alignment.IsReverseStrand(), qual, qualstr,
                    alignment.MapQuality);
            DEBUG2(allele);
            ra.alleles.push_back(allele);

            sp += l;  // update sample position
            csp += l;

        } else if (t == 'I') { // insertion

            string qualstr = rQual.substr(rp, l);

            // calculate joint quality, which is the probability that there are no errors in the observed bases
            vector<short> quals = qualities(qualstr);
            short qual = *max_element(quals.begin(), quals.end());
            if (qual >= parameters.BQL2) {
                ra.mismatches += l;
            }
            if (qual >= parameters.BQL2) {
                indelMask[sp - alignment.Position] = true;
                indelMask[sp - alignment.Position + 1] = true;
            }
            // register insertion + base quality with reference sequence
            // XXX this cutoff may not make sense for long indels... the joint
            // quality is much lower than the 'average' quality
            Allele* allele = new Allele(ALLELE_INSERTION,
                    currentTarget->seq, sp, &currentPosition, &currentReferenceBase, l, "", rDna.substr(rp, l),
                    sampleName, alignment.Name, !alignment.IsReverseStrand(), qual,
                    qualstr, alignment.MapQuality);
            DEBUG2(allele);
            ra.alleles.push_back(allele);

            rp += l;

        } else if (t == 'S') { // soft clip, clipped sequence present in the read not matching the reference
            rp += l; sp += l; csp += l;
        } else if (t == 'H') { // hard clip on the read, clipped sequence is not present in the read
            sp += l; csp += l;
        } else if (t == 'N') { // skipped region in the reference
            sp += l; csp += l;
        }
        // padding is currently not handled
        //} else if (t == 'P') { // padding, silent deletion from the padded reference sequence
        //    sp += l; csp += l;
        //}
    } // end cigar iter loop
    //cerr << ra << endl;

    if (parameters.IDW > -1) {
        for (vector<bool>::iterator m = indelMask.begin(); m < indelMask.end(); ++m) {
            if (*m) {
                vector<bool>::iterator q = m - parameters.IDW;
                if (q < indelMask.begin()) q = indelMask.begin();
                for (; q <= m + parameters.IDW && q != indelMask.end(); ++q) {
                    *q = true;
                }
                m += parameters.IDW + 1;
            }
        }
        for (vector<Allele*>::iterator a = ra.alleles.begin(); a != ra.alleles.end(); ++a) {
            Allele& allele = **a;
            int start = (allele.position - alignment.Position);
            int end = start + allele.length;
            vector<bool>::iterator im = indelMask.begin();
            // if there is anything masked, store it, otherwise just leave the
            // indelMask on this alignment empty, which means, "no masking" in
            // Allele::masked()
            for (vector<bool>::iterator q = im + start; q != im + end; ++q) {
                if (*q) { // there is a masked element
                    allele.indelMask.resize(allele.length);
                    copy(im + start, im + end, allele.indelMask.begin());
                    break;
                }
            }
            // here apply the indel exclusion window to the allele
        }
    }

    return ra;

}


void AlleleParser::updateAlignmentQueue(void) {

    DEBUG2("updating alignment queue");

    // push to the front until we get to an alignment that doesn't overlap our
    // current position or we reach the end of available alignments
    // filter input reads; only allow mapped reads with a certain quality
    DEBUG2("currentAlignment.Position == " << currentAlignment.Position << ", currentPosition == " << currentPosition);
    if (currentAlignment.Position <= currentPosition) {
        do {
            DEBUG2("currentAlignment.Name == " << currentAlignment.Name);
            // get read group, and map back to a sample name
            string readGroup;
            if (!currentAlignment.GetTag("RG", readGroup)) {
                ERROR("Couldn't find read group id (@RG tag) for BAM Alignment " <<
                        currentAlignment.Name << " at position " << currentPosition
                        << " in sequence " << currentSequence << " EXITING!");
                exit(1);
            }

            // skip this alignment if we are not analyzing the sample it is drawn from
            if (readGroupToSampleNames.find(readGroup) == readGroupToSampleNames.end())
                continue;

            if (currentAlignment.IsDuplicate() && !parameters.useDuplicateReads)  // currently we don't process duplicate-marked reads
                continue;

            if (!currentAlignment.IsMapped())
                continue;

            // otherwise, get the sample name and register the alignment to generate a sequence of alleles
            string sampleName = readGroupToSampleNames[readGroup];
            // we have to register the alignment to acquire some information required by filters
            // such as mismatches
            // filter low mapping quality (what happens if MapQuality is not in the file)
            if (currentAlignment.MapQuality >= parameters.MQL0) {
                // checks if we should grab and cache more sequence in order to process this alignment
                int rightgap = (currentAlignment.AlignedBases.size() + currentAlignment.Position) 
                    - (currentTarget->right - 1 + basesAfterCurrentTarget);
                if (rightgap > 0) { extendReferenceSequence(rightgap); }
                RegisteredAlignment ra = registerAlignment(currentAlignment, sampleName);
                // TODO filters to implement:
                // duplicates --- tracked via each BamAlignment
                if (ra.mismatches <= parameters.RMU) {
                    registeredAlignmentQueue.push_front(ra);
                    for (vector<Allele*>::const_iterator allele = ra.alleles.begin(); allele != ra.alleles.end(); ++allele) {
                        registeredAlleles.push_back(*allele);
                    }
                }
            }
                // TODO collect statistics here...
        } while (bamMultiReader.GetNextAlignment(currentAlignment) && currentAlignment.Position <= currentPosition);
    }

    DEBUG2("... finished pushing new alignments");

    // pop from the back until we get to an alignment that overlaps our current position
    if (registeredAlignmentQueue.size() > 0) {
        BamAlignment* alignment = &registeredAlignmentQueue.back().alignment;
        while (currentPosition > alignment->GetEndPosition() && registeredAlignmentQueue.size() > 0) {
            DEBUG2("popping alignment");
            registeredAlignmentQueue.pop_back();
            if (registeredAlignmentQueue.size() > 0) {
                alignment = &registeredAlignmentQueue.back().alignment;
            } else {
                break;
            }
        }
    }

    DEBUG2("... finished popping old alignments");
}

void AlleleParser::updateRegisteredAlleles(void) {

    // remove reference alleles which are no longer overlapping the current position
    // http://stackoverflow.com/questions/347441/erasing-elements-from-a-vector
    vector<Allele*>& alleles = registeredAlleles;
    for (vector<Allele*>::iterator allele = alleles.begin(); allele != alleles.end(); ++allele) {
        if (currentPosition >= (*allele)->position + (*allele)->length) {
            delete *allele;
            *allele = NULL;
        }
    }
    alleles.erase(remove(alleles.begin(), alleles.end(), (Allele*)NULL), alleles.end());

}

// TODO change these to handle indels properly

void AlleleParser::removeNonOverlappingAlleles(vector<Allele*>& alleles) {
    for (vector<Allele*>::iterator allele = alleles.begin(); allele != alleles.end(); ++allele) {
        if (currentPosition >= (*allele)->position + (*allele)->length) {
            *allele = NULL;
        }
    }
    alleles.erase(remove(alleles.begin(), alleles.end(), (Allele*)NULL), alleles.end());
}

// removes alleles which are filtered at the current position, and unsets their 'processed' flag so they are later evaluated
void AlleleParser::removeFilteredAlleles(vector<Allele*>& alleles) {
    for (vector<Allele*>::iterator allele = alleles.begin(); allele != alleles.end(); ++allele) {
        if ((*allele)->quality < parameters.BQL0 || (*allele)->masked() || (*allele)->currentBase == "N") {
            (*allele)->processed = false; // force re-processing later
            *allele = NULL;
        }
    }
    alleles.erase(remove(alleles.begin(), alleles.end(), (Allele*)NULL), alleles.end());
}

// steps our position/beddata/reference pointers through all positions in all
// targets, returns false when we are finished
//
// pushes and pulls alignments out of our queue of overlapping alignments via
// updateAlignmentQueue() as we progress

/// XXX TODO  ... we MUST deal with updating the target reference sequence here
//  ... also need to have various checks here; 
//  1) invalid reference id
//  2) no alignments in region
//  3) failed jump to target start
//  ...

// returns true if we still have more targets to process
// false otherwise
bool AlleleParser::toNextTarget(void) {

    DEBUG2("seeking to next target with alignments...");

    bool ok = false;

    if (currentTarget == NULL)
        ok = loadTarget(&targets.front());

    while (!ok && currentTarget != &targets.back())
        ok = loadTarget(++currentTarget);

    if (ok) justSwitchedTargets = true;
    return ok;

}

bool AlleleParser::loadTarget(BedTarget* target) {

    currentTarget = target;

    DEBUG("processing target " << currentTarget->desc << " " <<
            currentTarget->seq << " " << currentTarget->left << " " <<
            currentTarget->right);

    DEBUG2("loading target reference subsequence");
    int refSeqID = bamMultiReader.GetReferenceID(currentTarget->seq);
    DEBUG2("reference sequence id " << refSeqID);

    DEBUG2("setting new position " << currentTarget->left);
    currentPosition = currentTarget->left - 1; // our bed targets are always 1-based at the left

    // TODO double-check the basing of the target end... is the setregion call 0-based 0-base non-inclusive?
    bool r = bamMultiReader.SetRegion(refSeqID, currentTarget->left - 1, refSeqID, currentTarget->right - 1);
    if (!r) {
        ERROR("Could not SetRegion to " << currentTarget->seq << ":" << currentTarget->left << ".." << currentTarget->right);
        return r;
    }

    DEBUG2("set region");

    r &= bamMultiReader.GetNextAlignment(currentAlignment);
    if (!r) {
        ERROR("Could not find any reads in target region " << currentTarget->seq << ":" << currentTarget->left << ".." << currentTarget->right);
        return r;
    }
    DEBUG2("got first alignment in target region");

    int left_gap = currentPosition - currentAlignment.Position;

    DEBUG2("left gap: " << left_gap << " currentAlignment.Position: " << currentAlignment.Position);

    //int right_gap = maxPos - currentTarget->right;
    // XXX the above is deprecated, as we now update as we read
    loadReferenceSequence(currentTarget, (left_gap > 0) ? left_gap : 0, 0);
    currentReferenceBase = currentReferenceBaseChar();

    DEBUG2("clearing registered alignments and alleles");
    registeredAlignmentQueue.clear();
    for (vector<Allele*>::iterator allele = registeredAlleles.begin(); allele != registeredAlleles.end(); ++allele) {
        delete *allele;
    }
    registeredAlleles.clear();

    return r;

}

// stepping
//
// if the next position is outside of target region
// seek to next target which is in-bounds for its sequence
// if none exist, return false
bool AlleleParser::toNextTargetPosition(void) {

    if (currentTarget == NULL) {
        if (!toNextTarget()) {
            return false;
        }
    } else {
        ++currentPosition;
        currentReferenceBase = currentReferenceBaseChar();
    }
    if (currentPosition >= currentTarget->right - 1) { // time to move to a new target
        DEBUG2("next position " << currentPosition + 1 <<  " outside of current target right bound " << currentTarget->right);
        if (!toNextTarget()) {
            DEBUG("no more targets, finishing");
            return false;
        }
    }
    DEBUG2("processing position " << currentPosition + 1 << " in sequence " << currentTarget->seq);
    updateAlignmentQueue();
    DEBUG2("updating registered alleles");
    updateRegisteredAlleles();
    return true;
}

// XXX for testing only, steps targets but does nothing
bool AlleleParser::dummyProcessNextTarget(void) {

    if (!toNextTarget()) {
        DEBUG("no more targets, finishing");
        return false;
    }

    while (bamMultiReader.GetNextAlignment(currentAlignment)) {
    }

    //DEBUG2("processing position " << currentPosition + 1 << " in sequence " << currentTarget->seq);
    return true;
}

bool AlleleParser::getNextAlleles(Samples& samples, int allowedAlleleTypes) {
    if (toNextTargetPosition()) {
        getAlleles(samples, allowedAlleleTypes);
        return true;
    } else {
        return false;
    }
}

void AlleleParser::getAlleles(Samples& samples, int allowedAlleleTypes) {

    DEBUG2("getting alleles");

    // if we just switched targets, clean up everything in our input vector
    if (justSwitchedTargets) {
        for (Samples::iterator s = samples.begin(); s != samples.end(); ++s)
            s->second.clear();
        justSwitchedTargets = false; // TODO this whole flagged stanza is hacky;
                                     // to clean up, store the samples map in
                                     // the AlleleParser and clear it when we jump
    } else {
        // otherwise, update and remove non-overlapping and filtered alleles
        for (Samples::iterator s = samples.begin(); s != samples.end(); ++s) {
            Sample& sample = s->second;
            for (Sample::iterator g = sample.begin(); g != sample.end(); ++g) {
                removeNonOverlappingAlleles(g->second); // removes alleles which no longer overlap our current position
                updateAllelesCachedData(g->second);  // calls allele.update() on each Allele*
                removeFilteredAlleles(g->second); // removes alleles which are filtered at this position, 
                                                  // and requeues them for processing by unsetting their 'processed' flag
            }
            sample.sortAlleles();
        }
    }

    // add the reference allele to the analysis
    if (parameters.useRefAllele) {
        if (currentReferenceAllele != NULL) delete currentReferenceAllele; // clean up after last position
        currentReferenceAllele = referenceAllele(parameters.MQR, parameters.BQR);
        samples[currentTarget->seq].clear();
        samples[currentTarget->seq][currentReferenceAllele->currentBase].push_back(currentReferenceAllele);
        //alleles.push_back(currentReferenceAllele);
    }

    // get the variant alleles *at* the current position
    // and the reference alleles *overlapping* the current position
    for (vector<Allele*>::const_iterator a = registeredAlleles.begin(); a != registeredAlleles.end(); ++a) {
        Allele& allele = **a;
        if (!allele.processed
                && allowedAlleleTypes & allele.type
                && (
                    (allele.type == ALLELE_REFERENCE 
                      && currentPosition >= allele.position 
                      && currentPosition < allele.position + allele.length) // 0-based, means position + length - 1 is the last included base
                  || 
                    (allele.position == currentPosition)
                    ) 
                ) {
            allele.update();
            if (allele.quality >= parameters.BQL0 && !allele.masked() && allele.currentBase != "N") {
                samples[allele.sampleID][allele.currentBase].push_back(*a);
                allele.processed = true;
            }
        }
    }

    // now remove empty alleles from our return so as to not confuse processing
    for (Samples::iterator s = samples.begin(); s != samples.end(); ++s) {
        const string& name = s->first;
        Sample& sample = s->second;
        bool empty = true;
        // now move updated alleles to the right bin
        sample.sortAlleles();
        // and remove any empty groups which remain
        for (Sample::iterator g = sample.begin(); g != sample.end(); ++g) {
            if (g->second.size() == 0) {
                //cerr << "sample " << name << " has an empty " << g->first << endl;
                sample.erase(g);
            } else {
                empty = false;
            }
        }
        // and remove the entire sample if it has no alleles
        if (empty) {
            samples.erase(s);
        }
    }

    // XXX for some reason we have to iterate through the samples *again* to find empty individuals
    // If this is nested within the above loop, it will fail to find the empty
    // individuals.  I am unsure why, except perhaps maps can have their
    // iterators invalidated if the above methods are used.
    for (Samples::iterator s = samples.begin(); s != samples.end(); ++s) {
        const string& name = s->first;
        Sample& sample = s->second;
        for (Sample::iterator g = sample.begin(); g != sample.end(); ++g) {
            if (g->second.size() == 0) {
                sample.erase(g);
            }
        }
    }

    DEBUG2("done getting alleles");

}

Allele* AlleleParser::referenceAllele(int mapQ, int baseQ) {
    string base = string(1, currentReferenceBase);
    //string name = reference->filename;
    string name = currentTarget->seq; // this behavior matches old bambayes
    string baseQstr = "";
    //baseQstr += qualityInt2Char(baseQ);
    Allele* allele = new Allele(ALLELE_REFERENCE, 
            currentTarget->seq,
            currentPosition,
            &currentPosition, 
            &currentReferenceBase,
            1, base, base, name, name,
            true, baseQ,
            baseQstr,
            mapQ);
    allele->genotypeAllele = true;
    allele->baseQualities.push_back(baseQ);
    allele->update();
    return allele;
}

vector<Allele> AlleleParser::genotypeAlleles(
        map<string, vector<Allele*> >& alleleGroups, // alleles grouped by equivalence
        Samples& samples, // alleles grouped by sample
        vector<Allele>& allGenotypeAlleles      // all possible genotype alleles, 
                                                // to add back alleles if we don't have enough to meet our minimum allele count
        ) {

    vector<pair<Allele, int> > unfilteredAlleles;

    DEBUG2("getting genotype alleles");

    for (map<string, vector<Allele*> >::iterator group = alleleGroups.begin(); group != alleleGroups.end(); ++group) {
        // for each allele that we're going to evaluate, we have to have at least one supporting read with
        // map quality >= MQL1 and the specific quality of the allele has to be >= BQL1
        DEBUG2("allele group " << group->first);
        bool passesFilters = false;
        int qSum = 0;
        vector<Allele*>& alleles = group->second;
        for (vector<Allele*>::iterator a = alleles.begin(); a != alleles.end(); ++a) {
            DEBUG2("allele " << **a);
            Allele& allele = **a;
            qSum += allele.quality;
        }
        Allele& allele = *(alleles.front());
        int length = (allele.type == ALLELE_REFERENCE || allele.type == ALLELE_SNP) ? 1 : allele.length;
        unfilteredAlleles.push_back(make_pair(genotypeAllele(allele.type, allele.currentBase, length), qSum));
    }
    DEBUG2("found genotype alleles");

    map<Allele, int> filteredAlleles;

    DEBUG2("filtering genotype alleles which are not supported by at least " << parameters.minAltCount 
            << " observations comprising at least " << parameters.minAltFraction << " of the observations in a single individual");
    for (vector<pair<Allele, int> >::iterator p = unfilteredAlleles.begin();
            p != unfilteredAlleles.end(); ++p) {

        Allele& genotypeAllele = p->first;
        int qSum = p->second;

        for (Samples::iterator s = samples.begin(); s != samples.end(); ++s) {
            Sample& sample = s->second; 
            int alleleCount = 0;
            Sample::iterator c = sample.find(genotypeAllele.currentBase);
            if (c != sample.end())
                alleleCount = c->second.size();
            int observationCount = sample.observationCount();
            if (alleleCount >= parameters.minAltCount 
                    && ((float) alleleCount / (float) observationCount) >= parameters.minAltFraction) {
                DEBUG2(genotypeAllele << " has support of " << alleleCount 
                    << " in individual " << s->first << " and fraction " 
                    << (float) alleleCount / (float) observationCount);
                filteredAlleles[genotypeAllele] = qSum;
                break;
                //out << *genotypeAllele << endl;
            }
        }
    }
    DEBUG2("filtered genotype alleles");


    vector<Allele> resultAlleles;

    if (parameters.useBestNAlleles == 0) {
        // this means "use everything"
        for (map<Allele, int>::iterator p = filteredAlleles.begin();
                p != filteredAlleles.end(); ++p) {
            resultAlleles.push_back(p->first);
        }
    } else {
        // this means, use the N best
        vector<pair<Allele, int> > sortedAlleles;
        for (map<Allele, int>::iterator p = filteredAlleles.begin();
                p != filteredAlleles.end(); ++p) {
            sortedAlleles.push_back(make_pair(p->first, p->second));
        }
        DEBUG2("sorting alleles to get best alleles");
        AllelePairIntCompare alleleQualityCompare;
        sort(sortedAlleles.begin(), sortedAlleles.end(), alleleQualityCompare);

        DEBUG2("getting N best alleles");
        string refBase = string(1, currentReferenceBase);
        bool hasRefAllele = false;
        for (vector<pair<Allele, int> >::iterator a = sortedAlleles.begin(); a != sortedAlleles.end(); ++a) {
            if (a->first.currentBase == refBase)
                hasRefAllele = true;
            resultAlleles.push_back(a->first);
            if (resultAlleles.size() >= parameters.useBestNAlleles) {
                break;
            }
        }
        DEBUG2("found " << sortedAlleles.size() << " alleles of which we now have " << resultAlleles.size());

        // if we have reached the limit of allowable alleles, and still
        // haven't included the reference allele, include it
        if (parameters.forceRefAllele && !hasRefAllele) {
            DEBUG2("including reference allele");
            resultAlleles.insert(resultAlleles.begin(), genotypeAllele(ALLELE_REFERENCE, refBase, 1));
        }

        // if we now have too many alleles (most likely one too many), get rid of some
        while (resultAlleles.size() > parameters.useBestNAlleles) {
            resultAlleles.pop_back();
        }

    }

    return resultAlleles;

}

// homopolymer run length.  number of consecutive nucleotides (prior to this
// position) in the genome reference sequence matching the alternate allele,
// after substituting the alternate in place of the reference sequence allele
int AlleleParser::homopolymerRunLeft(string altbase) {

    int position = currentPosition - 1;
    int sequenceposition = (position - (currentTarget->left - 1)) + basesBeforeCurrentTarget;
    int runlength = 0;
    while (sequenceposition >= 0 && currentSequence.substr(sequenceposition, 1) == altbase) {
        ++runlength;
        --position;
        sequenceposition = (position - (currentTarget->left - 1)) + basesBeforeCurrentTarget;
    }
    return runlength;

}

int AlleleParser::homopolymerRunRight(string altbase) {

    int position = currentPosition + 1;
    int sequenceposition = (position - (currentTarget->left - 1)) + basesBeforeCurrentTarget;
    int runlength = 0;
    while (sequenceposition >= 0 && currentSequence.substr(sequenceposition, 1) == altbase) {
        ++runlength;
        ++position;
        sequenceposition = (position - (currentTarget->left - 1)) + basesBeforeCurrentTarget;
    }
    return runlength;

}
