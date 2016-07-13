Architecture
============

Bangra understands and transforms source code in multiple cleanly separated stages:

=====  ===========  =======================  ====================
Order  Stage        Through                  To
=====  ===========  =======================  ====================
1      Parsing      Data Interchange Format  S-Expression Tree
2      Expansion    Bangra Language          Special Forms Only
3      Translation  IR Language              LLVM Module
4      Execution    JIT Generated Functions  Program Output
=====  ===========  =======================  ====================

TODO