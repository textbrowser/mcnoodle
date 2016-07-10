extern "C"
{
#include <inttypes.h>
}

#include <bitset>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/nondet_random.hpp>
#include <boost/numeric/ublas/matrix.hpp>
#include <boost/numeric/ublas/lu.hpp>
#include <boost/random.hpp>
#include <map>

#include "mcnoodle.h"

mcnoodle::mcnoodle(const size_t k,
		   const size_t n,
		   const size_t t)
{
  m_k = minimumK(k);
  m_n = minimumN(n);
  m_t = minimumT(t);
  m_Gcar.resize(m_k, m_n);
  m_P.resize(m_n, m_n);
  m_Pinv.resize(m_n, m_n);
  m_S.resize(m_k, m_k);
  m_Sinv.resize(m_k, m_k);
}

mcnoodle::mcnoodle
(const boost::numeric::ublas::matrix<mcnoodle_matrix_element_type_t> &Gcar,
 const size_t k,
 const size_t n,
 const size_t t)
{
  m_Gcar = Gcar;
  m_k = minimumK(k);
  m_n = minimumN(n);
  m_t = minimumT(t);
}

mcnoodle::~mcnoodle()
{
}

bool mcnoodle::decrypt(const char *ciphertext, const size_t ciphertext_size,
		       char *plaintext, size_t *plaintext_size)
{
  bool ok = false;

  if(!ciphertext || ciphertext_size <= 0 || !plaintext || !plaintext_size)
    goto done_label;

 done_label:
  return ok;
}

bool mcnoodle::encrypt(const char *plaintext, const size_t plaintext_size,
		       char *ciphertext, size_t *ciphertext_size)
{
  if(!ciphertext_size || !plaintext || plaintext_size <= 0)
    return false;

  if(CHAR_BIT * plaintext_size > m_k)
    return false;

  /*
  ** Represent the message as a binary vector of length k.
  */

  boost::numeric::ublas::matrix<mcnoodle_matrix_element_type_t> m
    (1, m_k, 0);

  for(size_t i = 0, k = 0; i < plaintext_size; i++)
    {
      std::bitset<CHAR_BIT> b(plaintext[i]);

      for(size_t j = 0; j < b.size(); j++, k++)
	m(0, k) = b[j];
    }

  /*
  ** Generate a random binary vector of length n having at most t 1s.
  */

  boost::numeric::ublas::matrix<mcnoodle_matrix_element_type_t> z(1, m_n, 0);
  boost::random::uniform_int_distribution<uint64_t> distribution;
  boost::random_device random_device;
  size_t ts = 0;

  for(size_t i = 0; i < z.size2(); i++)
    {
      mcnoodle_matrix_element_type_t a = distribution(random_device) % 2;

      if(a == 1)
	{
	  ts += 1;
	  z(0, i) = 1;

	  if(m_t == ts)
	    break;
	}
    }

  /*
  ** Compute c = mGcar + z.
  */

  boost::numeric::ublas::matrix<mcnoodle_matrix_element_type_t> c(1, m_n);

  c = boost::numeric::ublas::prod(m, m_Gcar) + z;

  /*
  ** Place c into ciphertext. The user is responsible for restoring memory.
  */

  *ciphertext_size = static_cast<size_t> (std::ceil(c.size2() / CHAR_BIT));
  ciphertext = new char[*ciphertext_size];

  for(size_t i = 0, k = 0; i < c.size2(); k++)
    {
      char a = 0;

      for(size_t j = 0; i < c.size2() && j < CHAR_BIT; i++, j++)
	if((c(0, i) >> (j + 1)) & 1)
	  a |= static_cast<char> (1 << (j + 1));

      ciphertext[k] = a;
    }

  return true;
}

void mcnoodle::prepareP(void)
{
  /*
  ** Generate an n x n random permutation matrix and discover its inverse.
  */

  boost::numeric::ublas::matrix<mcnoodle_matrix_element_type_t> P(m_n, m_n, 0);
  boost::random::uniform_int_distribution<uint64_t> distribution;
  boost::random_device random_device;
  std::map<size_t, char> indexes;

  /*
  ** 0 ... 1 ... 0 ... 0 ...
  ** 1 ... 0 ... 0 ... 0 ...
  ** 0 ... 0 ... 1 ... 0 ...
  ** 0 ... 0 ... 0 ... 0 ...
  ** 0 ... 0 ... 0 ... 1 ...
  ** ...
  */

  for(size_t i = 0; i < P.size1(); i++)
    do
      {
	size_t j = distribution(random_device) % P.size2();

	if(indexes.find(j) == indexes.end())
	  {
	    P(i, j) = 1;
	    indexes[j] = 0;
	    break;
	  }
      }
    while(true);

  /*
  ** A permutation matrix always has an inverse.
  */

  /*
  ** (PP^T)ij = Sum(Pik(P^T)kj, k = 1..n) = Sum(PikPjk, k = 1..n).
  ** Sum(PikPjk, k = 1..n) = 1 if i = j, and 0 otherwise (I).
  ** That is, PP^T = I or the inverse of P is equal to P's transpose.
  */

  m_P = P;
  m_Pinv = boost::numeric::ublas::trans(m_P);
}

void mcnoodle::prepareS(void)
{
  /*
  ** Generate a random k x k binary non-singular matrix and its inverse.
  */

  boost::numeric::ublas::matrix<float> S; /*
					  ** BOOST type-checking may
					  ** raise an exception unless
					  ** we're using real numbers.
					  */
  boost::random::uniform_int_distribution<uint64_t> distribution;
  boost::random_device random_device;

  S.resize(m_k, m_k);

  for(size_t i = 0; i < S.size1(); i++)
    for(size_t j = 0; j < S.size2(); j++)
      S(i, j) = static_cast<float> (distribution(random_device) % 2);

  m_S = S;
}

void mcnoodle::serialize
(char *buffer,
 const size_t buffer_size,
 const boost::numeric::ublas::matrix<mcnoodle_matrix_element_type_t> &m)
{
  if(!buffer || buffer_size <= 0)
    return;

  boost::iostreams::array_sink sink(buffer, buffer_size);
  boost::iostreams::stream<boost::iostreams::array_sink> source(sink);
  boost::archive::binary_oarchive archive(source);

  archive << m;
}

void mcnoodle::serializeGcar(char *buffer, const size_t buffer_size)
{
  serialize(buffer, buffer_size, m_P);
}

void mcnoodle::serializePinv(char *buffer, const size_t buffer_size)
{
  serialize(buffer, buffer_size, m_Pinv);
}

void mcnoodle::serializeSinv(char *buffer, const size_t buffer_size)
{
  serialize(buffer, buffer_size, m_Sinv);
}
