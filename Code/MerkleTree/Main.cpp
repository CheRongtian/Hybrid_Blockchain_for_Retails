#include <iostream>
#include "MerkleTree.hpp"


bool menu(MerkleTree &T)
{
    std::cout << std::endl << std::endl << "\tA)dd a new block" << std::endl;
    std::cout << "\tR)equest a block" << std::endl;
    std::cout << "\tV)erify the proof" << std::endl;
    std::cout << "\tT)raverse the tree" << std::endl;
    std::cout << "\tQ)uit" << std::endl;
    std::cout << std::endl << "\tEnter your command: ";

    static std::string strResult = "";

    std::string strBlock("");
    // string strResult = "";
    int nBlock = -1;
    char ch = ' ';
    std::cin >> ch;

    // while(ch == 10) cin.get(ch);
    switch(ch)
    {
        case 'a':
        case 'A':
            std::cout << "\n\tEnter new block text: ";
            std::cin >> strBlock;
            T.Append(strBlock);
            return true;
        
        case 'v':
        case 'V':
            T.Verify(strResult);
            return true;
        
        case 't':
        case 'T':
            T.PrintTree();
            return true;
        
        case 'r':
        case 'R':
            std::cout << "\n\tEnter block no: (Type 'q' or any letter to return)";
            // cin >> nBlock;
            while(std::cin >>nBlock && nBlock != -1)
            {
                strResult = T.ProverBlock(nBlock);
                std::cout << strResult << std::endl;
                std::cout << "\n\tEnter block no: (Type 'q' or any letter to return):";
            }

            if(std::cin.fail())
            {
                std::cin.clear();
                std::string dummy;
                std::cin >> dummy;
            }
            return true;

        case 'q':
        case 'Q':
            return false;
    }
    return false;
}

int main()
{
    MerkleTree T(70); // Give the block size (here it is 70 bytes)
    T.Build("inp.txt"); // Give you input file name
    while(menu(T));
    return 0;
}
