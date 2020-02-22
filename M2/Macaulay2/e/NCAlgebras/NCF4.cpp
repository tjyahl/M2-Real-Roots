#include "../text-io.hpp"
#include "NCAlgebras/NCF4.hpp"

NCF4::NCF4(const FreeAlgebra& A,
           const ConstPolyList& input,
           int hardDegreeLimit,
           int strategy
           )
    : mFreeAlgebra(A),
      mInput(input),
      mTopComputedDegree(-1),
      mHardDegreeLimit(hardDegreeLimit),
      mMonomEq(A.monoid()),
      mColumnMonomials(mMonomEq)
{
  if (M2_gbTrace >= 1)
    {
      buffer o;
      o << "[NCGB F4]";
      emit_wrapped(o.str());
    }
  
  // process input polynomials
  mIsGraded = true;
  for (auto i = 0; i < mInput.size(); ++i)
    {
      auto d = freeAlgebra().heft_degree(*mInput[i]);
      if (not d.second)
        mIsGraded = false;
      mOverlapTable.insert(d.first,
                           true,
                           std::make_tuple(i,-1,-1));
    }
  if (M2_gbTrace >= 1)
    {
      buffer o;
      o << (mIsGraded ? " homogeneous " : " inhomogeneous ");
      emit_wrapped(o.str());
    }
}

void NCF4::compute(int softDegreeLimit)
{
  while (!mOverlapTable.isFinished(softDegreeLimit))
    {
      auto degSet = mOverlapTable.nextDegreeOverlaps();
      auto toBeProcessed = degSet.second;
      if (M2_gbTrace >= 1)
        {
          buffer o;
          o << "{" << degSet.first << "}(" << toBeProcessed->size() << ")";
          emit_wrapped(o.str());
        }
      process(*toBeProcessed);
      mOverlapTable.removeLowestDegree(); // TODO: suspect line.
      // we really want to just delete toBeProcessed...
    }
}

void NCF4::process(const std::deque<Overlap>& overlapsToProcess)
{
  buildF4Matrix(overlapsToProcess);

  // reduce it

  // auto-reduce the new elements
  
  // convert back to GB elements...
}

void NCF4::matrixReset()
{
  mMonomialSpace.deallocateAll();
  mReducersTodo.clear();
  mOverlapsTodo.clear();
  mColumns.clear();
  mReducers.clear();
  mOverlaps.clear();
  mCurrentReducer = 0;
  mCurrentOverlap = 0;
}

void NCF4::preRowsFromOverlap(const Overlap& o)
{
  // o = (gbLeftIndex, overLapPos, gbRightIndex).
  // BUT: if overlapPos < 0, then gbRightIndex is also < 0 (and is ignored), and gbLeftIndex
  //   refers to a generator, and we add to mOverlapsTodo in only (1,gbLeftIndex,1).
  // where 1 = empty word.

  int gbLeftIndex = std::get<0>(o);
  int overlapPos = std::get<1>(o);
  int gbRightIndex = std::get<2>(o);

  if (overlapPos < 0)
    {
      // Sneaky trick: a PreRow with index a < 0 refers to generator with index -a-1
      mOverlapsTodo.push_back(PreRow(Word(), - gbLeftIndex - 1, Word()));
      return;
    }
  
  // LM(gbLeft) = x^a x^b
  // LM(gbRight) = x^b x^c
  // overlapPos = starting position of x^b in gbLeft.
  // first prerow will be: (1, gbLeftIndex, x^c)
  // second prerow will be: (x^a, gbRightIndex, 1)
      
  std::cout << gbLeftIndex << " " << overlapPos << " " << gbRightIndex << std::endl;

  Word leadWordLeft = freeAlgebra().lead_word(*mGroebner[gbLeftIndex]);
  Word leadWordRight = freeAlgebra().lead_word(*mGroebner[gbRightIndex]);
  int overlapLen = leadWordLeft.size() - overlapPos;

  std::cout << leadWordLeft << " " << leadWordRight << " " << overlapLen << std::endl;      
  
  Word suffix2 {}; // trivial word
  Word prefix2(leadWordLeft.begin(), leadWordLeft.begin() + overlapPos);
  
  Word suffix1(leadWordRight.begin() + overlapLen, leadWordRight.end());
  Word prefix1 {}; // trivial word
  
  mOverlapsTodo.push_back(PreRow(prefix1, gbLeftIndex, suffix1));
  mReducersTodo.push_back(PreRow(prefix2, gbRightIndex, suffix2));
}

void NCF4::buildF4Matrix(const std::deque<Overlap>& overlapsToProcess)
{
  matrixReset();

  std::cout << "About to create PreRows from overlapsToProcess" << std::endl;
  for (auto o : overlapsToProcess)
    {
      preRowsFromOverlap(o);
    }

  std::cout << "About to process mReducersTodo" << std::endl;
  // process each element in mReducersTodo

  for ( ; mCurrentReducer < mReducersTodo.size(); ++mCurrentReducer)
    {
      Row r = processPreRow(mReducersTodo[mCurrentReducer]); // this often adds new elements to mReducersTodo
      mReducers.push_back(r);
    }

  for ( ; mCurrentOverlap < mOverlapsTodo.size(); ++mCurrentOverlap)
    {
      Row r = processPreRow(mOverlapsTodo[mCurrentOverlap]); // this often adds new elements to mReducersTodo
      mOverlaps.push_back(r);
    }

  for ( ; mCurrentReducer < mReducersTodo.size(); ++mCurrentReducer)
    {
      Row r = processPreRow(mReducersTodo[mCurrentReducer]); // this often adds new elements to mReducersTodo
      mReducers.push_back(r);
    }
}

NCF4::Row NCF4::processPreRow(PreRow r)
{
  std::cout << "processing PreRow("<< std::get<0>(r) << ", "
            << std::get<1>(r) << ", "
            << std::get<2>(r) << ")" << std::endl;

  Word left = std::get<0>(r);
  int gbIndex = std::get<1>(r);
  Word right = std::get<2>(r);

  Poly elem;
  if (gbIndex < 0)
    {
      freeAlgebra().copy(elem, *mInput[-gbIndex-1]);
    }
  else
    {
      freeAlgebra().mult_by_term_left_and_right(elem, *mGroebner[gbIndex], left, right);
    }

  // loop through all monomials of the product
  // for each monomial:
  //  is it in the hash table?
  //    if so: return the column index into the component for the new row.
  //    if not: insert it, and return the new column index into same place
  //        and place this monomial into mColumns.
  //        and search for divisor for it.
  //        

  int nterms = elem.numTerms();
  std::cout << "nterms = " << nterms << std::endl;
  auto componentRange = mMonomialSpace.allocateArray<int>(nterms);
  int* nextcolloc = componentRange.first;
  for (auto i = elem.cbegin(); i != elem.cend(); ++i)
    {
      std::cout << "next monomial: ";
      Monom m = i.monom();
      auto it = mColumnMonomials.find(m);
      if (it == mColumnMonomials.end())
        {
          auto rg = mMonomialSpace.allocateArray<int>(m.size());
          std::copy(m.begin(), m.end(), rg.first);
          Monom newmon = Monom(rg.first);
          auto divresult = findDivisor(newmon);
          int divisornum = (divresult.first ? mReducersTodo.size() : -1);
          int newColumnIndex = mColumnMonomials.size();
          mColumnMonomials.insert({newmon, {newColumnIndex, divisornum}});
          if (divresult.first) mReducersTodo.push_back(divresult.second);
          *nextcolloc++ = newColumnIndex;
          std::cout << "n" << newColumnIndex << " ";
        }
      else
        {
          *nextcolloc++ = (*it).second.first;
          std::cout << (*it).second.first << " ";
        }
    }
  std::cout << std::endl;
  // the following line causes a copy of the coeff vector of 'elem'.
  return(Row(elem.getCoeffVector(), componentRange));
}

std::pair<bool, NCF4::PreRow> NCF4::findDivisor(Monom mon)
{
  Word newword;
  freeAlgebra().monoid().wordFromMonom(newword, mon);
  std::pair<int,int> divisorInfo;
  bool found = mWordTable.subword(newword, divisorInfo);
  // if newword = x^a x^b x^c, with x^b in the word table, then:
  //  divisorInfo.first = index of the GB element with x^b as lead monomial.
  //  divisorInfo.second = position of the start of x^b in newword
  //   (that is, the length of x^a).
  if (not found)
    return std::make_pair(false, PreRow(Word(), 0, Word()));
  Word prefix = Word(newword.begin(), newword.begin() + divisorInfo.second);
  Word divisorWord = freeAlgebra().lead_word(*mGroebner[divisorInfo.first]);
  Word suffix = Word(newword.begin() + divisorInfo.second + divisorWord.size(),
                     newword.end());
  return std::make_pair(true, PreRow(prefix, divisorInfo.first, suffix));
}

// Local Variables:
// compile-command: "make -C $M2BUILDDIR/Macaulay2/e  "
// indent-tabs-mode: nil
// End:
