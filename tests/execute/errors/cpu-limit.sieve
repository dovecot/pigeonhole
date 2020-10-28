require ["mime","foreverypart","fileinto", "variables", "regex"];

# Here we create an inefficient regex with long compilation time
set "my_exp" "^(((A)|(AB)|(ABC)|(ABCD)|(ABCDE)|(ABCDEF)|(ABCDEFG)|(ABCDEFGH)|(ABCDEFGHI)|(ABCDEFGHIJ)|(ABCDEFGHIJK)|(ABCDEFGHIJKL)|(ABCDEFGHIJKLM)|(ABCDEFGHIJKLMN)|(ABCDEFGHIJKLMNO)|(ABCDEFGHIJKLMNOP)|(ABCDEFGHIJKLMNOPQ)|(ABCDEFGHIJKLMNOPQR))?((B)|(BC)|(BCD)|(BCDE)|(BCDEF)|(BCDEFG)|(BCDEFGH)|(BCDEFGHI)|(BCDEFGHIJ)|(BCDEFGHIJK)|(BCDEFGHIJKL)|(BCDEFGHIJKLM)|(BCDEFGHIJKLMN)|(BCDEFGHIJKLMNO)|(BCDEFGHIJKLMNOP)|(BCDEFGHIJKLMNOPQ)|(BCDEFGHIJKLMNOPQR))?((C)|(CD)|(CDE)|(CDEF)|(CDEFG)|(CDEFGH)|(CDEFGHI)|(CDEFGHIJ)|(CDEFGHIJK)|(CDEFGHIJKL)|(CDEFGHIJKLM)|(CDEFGHIJKLMN)|(CDEFGHIJKLMNO)|(CDEFGHIJKLMNOP)|(CDEFGHIJKLMNOPQ)|(CDEFGHIJKLMNOPQR))?((D)|(DE)|(DEF)|(DEFG)|(DEFGH)|(DEFGHI)|(DEFGHIJ)|(DEFGHIJK)|(DEFGHIJKL)|(DEFGHIJKLM)|(DEFGHIJKLMN)|(DEFGHIJKLMNO)|(DEFGHIJKLMNOP)|(DEFGHIJKLMNOPQ)|(DEFGHIJKLMNOPQR))?((E)|(EF)|(EFG)|(EFGH)|(EFGHI)|(EFGHIJ)|(EFGHIJK)|(EFGHIJKL)|(EFGHIJKLM)|(EFGHIJKLMN)|(EFGHIJKLMNO)|(EFGHIJKLMNOP)|(EFGHIJKLMNOPQ)|(EFGHIJKLMNOPQR))?((F)|(FG)|(FGH)|(FGHI)|(FGHIJ)|(FGHIJK)|(FGHIJKL)|(FGHIJKLM)|(FGHIJKLMN)|(FGHIJKLMNO)|(FGHIJKLMNOP)|(FGHIJKLMNOPQ)|(FGHIJKLMNOPQR))?((G)|(GH)|(GHI)|(GHIJ)|(GHIJK)|(GHIJKL)|(GHIJKLM)|(GHIJKLMN)|(GHIJKLMNO)|(GHIJKLMNOP)|(GHIJKLMNOPQ)|(GHIJKLMNOPQR))?((H)|(HI)|(HIJ)|(HIJK)|(HIJKL)|(HIJKLM)|(HIJKLMN)|(HIJKLMNO)|(HIJKLMNOP)|(HIJKLMNOPQ)|(HIJKLMNOPQR))?((I)|(IJ)|(IJK)|(IJKL)|(IJKLM)|(IJKLMN)|(IJKLMNO)|(IJKLMNOP)|(IJKLMNOPQ)|(IJKLMNOPQR))?((J)|(JK)|(JKL)|(JKLM)|(JKLMN)|(JKLMNO)|(JKLMNOP)|(JKLMNOPQ)|(JKLMNOPQR))?((K)|(KL)|(KLM)|(KLMN)|(KLMNO)|(KLMNOP)|(KLMNOPQ)|(KLMNOPQR))?((L)|(LM)|(LMN)|(LMNO)|(LMNOP)|(LMNOPQ)|(LMNOPQR))?((M)|(MN)|(MNO)|(MNOP)|(MNOPQ)|(MNOPQR))?((N)|(NO)|(NOP)|(NOPQ)|(NOPQR))?((O)|(OP)|(OPQ)|(OPQR))?((P)|(PQ)|(PQR))?((Q)|(QR))?((R))?((R)|(RQ)|(RQP)|(RQPO)|(RQPON)|(RQPONM)|(RQPONML)|(RQPONMLK)|(RQPONMLKJ)|(RQPONMLKJI)|(RQPONMLKJIH)|(RQPONMLKJIHG)|(RQPONMLKJIHGF)|(RQPONMLKJIHGFE)|(RQPONMLKJIHGFED)|(RQPONMLKJIHGFEDC)|(RQPONMLKJIHGFEDCB)|(RQPONMLKJIHGFEDCBA))?((Q)|(QP)|(QPO)|(QPON)|(QPONM)|(QPONML)|(QPONMLK)|(QPONMLKJ)|(QPONMLKJI)|(QPONMLKJIH)|(QPONMLKJIHG)|(QPONMLKJIHGF)|(QPONMLKJIHGFE)|(QPONMLKJIHGFED)|(QPONMLKJIHGFEDC)|(QPONMLKJIHGFEDCB)|(QPONMLKJIHGFEDCBA))?((P)|(PO)|(PON)|(PONM)|(PONML)|(PONMLK)|(PONMLKJ)|(PONMLKJI)|(PONMLKJIH)|(PONMLKJIHG)|(PONMLKJIHGF)|(PONMLKJIHGFE)|(PONMLKJIHGFED)|(PONMLKJIHGFEDC)|(PONMLKJIHGFEDCB)|(PONMLKJIHGFEDCBA))?((O)|(ON)|(ONM)|(ONML)|(ONMLK)|(ONMLKJ)|(ONMLKJI)|(ONMLKJIH)|(ONMLKJIHG)|(ONMLKJIHGF)|(ONMLKJIHGFE)|(ONMLKJIHGFED)|(ONMLKJIHGFEDC)|(ONMLKJIHGFEDCB)|(ONMLKJIHGFEDCBA))?((N)|(NM)|(NML)|(NMLK)|(NMLKJ)|(NMLKJI)|(NMLKJIH)|(NMLKJIHG)|(NMLKJIHGF)|(NMLKJIHGFE)|(NMLKJIHGFED)|(NMLKJIHGFEDC)|(NMLKJIHGFEDCB)|(NMLKJIHGFEDCBA))?((M)|(ML)|(MLK)|(MLKJ)|(MLKJI)|(MLKJIH)|(MLKJIHG)|(MLKJIHGF)|(MLKJIHGFE)|(MLKJIHGFED)|(MLKJIHGFEDC)|(MLKJIHGFEDCB)|(MLKJIHGFEDCBA))?((L)|(LK)|(LKJ)|(LKJI)|(LKJIH)|(LKJIHG)|(LKJIHGF)|(LKJIHGFE)|(LKJIHGFED)|(LKJIHGFEDC)|(LKJIHGFEDCB)|(LKJIHGFEDCBA))?((K)|(KJ)|(KJI)|(KJIH)|(KJIHG)|(KJIHGF)|(KJIHGFE)|(KJIHGFED)|(KJIHGFEDC)|(KJIHGFEDCB)|(KJIHGFEDCBA))?((J)|(JI)|(JIH)|(JIHG)|(JIHGF)|(JIHGFE)|(JIHGFED)|(JIHGFEDC)|(JIHGFEDCB)|(JIHGFEDCBA))?((I)|(IH)|(IHG)|(IHGF)|(IHGFE)|(IHGFED)|(IHGFEDC)|(IHGFEDCB)|(IHGFEDCBA))?((H)|(HG)|(HGF)|(HGFE)|(HGFED)|(HGFEDC)|(HGFEDCB)|(HGFEDCBA))?((G)|(GF)|(GFE)|(GFED)|(GFEDC)|(GFEDCB)|(GFEDCBA))?((F)|(FE)|(FED)|(FEDC)|(FEDCB)|(FEDCBA))?((E)|(ED)|(EDC)|(EDCB)|(EDCBA))?((D)|(DC)|(DCB)|(DCBA))?((C)|(CB)|(CBA))?((B)|(BA))?((A))?)+$";
set "a" "ABCDEFGHIJKLMNOPQR";
set "b" "RQPONMLKJIHGFEDCBA";
set "c" "${a}${b}${a}${b}${a}${b}${a}${b}";
set "e" "${c}${c}${c}${c}${c}${c}${c}${c}";
set "f" "${e}${e}${e}${e}${e}${e}${e}${e}";

# We create a string on which this regex will spend enough time (around 200 ms)
set "final" "${f}${f}${f}${f}${f}${f}${f}${f}${f}${f}${f}${f}${f}${f}@";

# We repeat the throttling process for every mime part
foreverypart {
	# We use several if statements to multiply the cpu time consumed by one match
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
	if string :regex  "${final}" "${my_exp}" { discard; }
	if string :regex  "${final}" "${my_exp}" { keep; }
}
