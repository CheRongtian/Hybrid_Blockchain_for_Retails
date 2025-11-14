#include <iostream>
#include <vector>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <vector>

using namespace std;
using uchar = unsigned char;

struct Node
{
    int blockID;
    string value, hashValue;
    Node *left, *right, *parent;
};

class MerkleTree
{
    Node *root;
    int blockSize, blockNo, levels;
    const int HASHSIZE = 2 * SHA256_DIGEST_LENGTH;

    public:
    MerkleTree(int Tsize) : blockSize(Tsize)
    {
        levels = 0;
        blockNo = -1;
    }
    string SHA256(const string strIn)
    {
        uchar hash_buf[SHA256_DIGEST_LENGTH];
        EVP_MD_CTX *ctx = EVP_MD_CTX_new();

        EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
        EVP_DigestUpdate(ctx, strIn.c_str(), strIn.size());
        EVP_DigestFinal_ex(ctx, hash_buf, NULL);
        EVP_MD_CTX_free(ctx);

        stringstream ss;
        for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) ss << hex <<setw(2) << setfill('0') << (int)hash_buf[i];
        return ss.str();
    }

    // this function receives the file name,
    // reads the text un chunks of the given size, and
    // builds the Merkle hash tree accordingly

    bool Build(const char* fileName)
    {
        ifstream inFile(fileName);
        if(!inFile.good()) return false;

        // Now, read the file block by block of the given size.
        // And, add all blocks into a list.
        vector<Node*> listChild, listParent;
        Node *leftN = nullptr, *rightN = nullptr;
        char buffer[blockSize + 1] ;
        buffer[blockSize] = '\0';
        while(!inFile.eof())
        {
            inFile.read(buffer, blockSize);
            if(!inFile.gcount()) continue;
            if(inFile.gcount() < blockSize)
            {
                for(int i=inFile.gcount(); i<blockSize; i++) buffer[i] = 'E';
            }
            Node *n = new Node;
            n->value = buffer;
            n->hashValue = SHA256(buffer);
            n->blockID = ++blockNo;
            n->left = n->right = n->parent = nullptr;
            listChild.push_back(n);
        }

        do
        {
            levels++;
        }while(pow(2, levels) <= blockNo);

        // And, build the tree over the blocks stored in the list
        do
        {
            auto iterP = listParent.begin();
            auto doneP = listParent.end();
            while(iterP != doneP) listChild.push_back(*iterP++);
            listParent.clear();

            auto iter = listChild.begin();
            auto done = listChild.end();
            leftN = rightN = nullptr;
            while(iter != done)
            {
                if(!leftN) leftN = *iter++;
                else
                {
                    rightN = *iter++;
                    Node *parentN = new Node;
                    parentN->value = leftN->value;
                    parentN->hashValue = SHA256(leftN->hashValue + rightN->hashValue);
                    parentN->left = leftN;
                    parentN->right = rightN;
                    parentN->parent = nullptr;
                    parentN->blockID = -1;
                    listParent.push_back(parentN);
                    leftN->parent = rightN->parent = parentN;
                    leftN = rightN = nullptr;
                }
            }

            if(leftN)
            {
                // Make a new NULL node
                rightN = new Node;
                rightN->hashValue = "";
                rightN->blockID = -1;
                rightN->left = rightN->right = nullptr;

                Node *parentN = new Node;
                parentN->value = leftN->value;
                parentN->hashValue = SHA256(leftN->hashValue + rightN->hashValue);
                parentN->left = leftN;
                parentN->right = rightN;
                parentN->parent = nullptr;
                parentN->blockID = -1;
                listParent.push_back(parentN);
                leftN->parent = rightN->parent = parentN;
                leftN = rightN = nullptr;
            }
            
            listChild.clear();
        }while(listParent.size() > 1);

        root = listParent[0];
        return true;
    }

    // Print the leaf values for test purposes
    void PrintTree()
    {
        Traverse(root);
        cout << endl;
    }

    void Traverse(const Node *tree)
    {
        if(!tree->left)
        {
            cout << tree->blockID << "\t" <<tree->hashValue << endl;
            return;
        }
        Traverse(tree->left);
        Traverse(tree->right);
    }
};

int main()
{
    return 0;
}