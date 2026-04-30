#include "JumpList.h"
#include <stdexcept>
#include <string>

using namespace std;

Node::Node(const string& s, Node* next, Node* jump, int gap) {
	data_ = s;
	next_ = next;
	jump_ = jump;
	gap_ = gap;
}

Node::~Node() {
	next_ = nullptr;
	jump_ = nullptr;
}


JumpList::JumpList() {
	head_ = nullptr;
}

// DO NOT CHANGE
// You do not need to understand the code here to complete the assignment
JumpList::JumpList(int size, const string* arr) {

	const string s[] = {"a", "b", "blah", "c", "cat", "d", "etc", "ooo...", "x", "zzz"};
	const string* sp = (arr == nullptr) ? s : arr;

	Node** npp = new Node*[size];
	npp[size-1] = new Node(sp[size-1], nullptr, nullptr, 0);
	for(int i = size-2; i >= 0; i--)
		npp[i] = new Node(sp[i], npp[i+1], nullptr, 0);

	if (arr == nullptr) {

		if (size==1) npp[0]->gap_ = 1;
		else if (size==3) npp[0]->gap_ = 3;
 		else if (size==10) {
			npp[0]->jump_ = npp[5];
			npp[5]->jump_ = npp[8];
			npp[0]->gap_ = 5;
			npp[5]->gap_ = 3;
			npp[8]->gap_ = 2;
		}
		else throw std::invalid_argument("size must be 1, 3 or 10");

	}
	else {

		// jump node every MAX_GAP_SIZE
		int i;
		for(i=0; i < size-MAX_GAP_SIZE; i += MAX_GAP_SIZE) {
			npp[i]->jump_ = npp[i+MAX_GAP_SIZE];
			npp[i]->gap_ = MAX_GAP_SIZE;
		}
		npp[i]->gap_ = size % MAX_GAP_SIZE;
		if (npp[i]->gap_ == 0) npp[i]->gap_ = MAX_GAP_SIZE; // 0 need to become 5!

	}

	head_ = npp[0];
	delete [] npp; // note that the local array npp gets destroyed here but the nodes stay alive!

}

JumpList::~JumpList() {
    Node* current = head_;
    while (current != nullptr) {
        Node* next = current->next_;  
        delete current;               
        current = next;               
    }
    head_ = nullptr; 
}


int JumpList::size() const {
    int nodeCount = 0;
    for (Node* currentNode = head_; 
         currentNode != nullptr; 
         currentNode = currentNode->next_) {
        nodeCount++;
    }
    return nodeCount;
}

// DONE FOR YOU, DO NOT CHANGE
bool JumpList::find(const string& s) const {

	if (head_ == nullptr) return false;

	// moving along the fast lane
	Node* tmp = head_;
	while(tmp->jump_ != nullptr && tmp->jump_->data_ < s)
		tmp = tmp->jump_;

	// tmp now points to the jump node at the start of the segment where s could be

	// now slow lane
	while(tmp != nullptr) {

		if (tmp->data_ == s) return true; // match
		else if (tmp->data_ > s) return false; // went past without finding s
		else tmp = tmp->next_;
	}

	return false; // end of list
}

string JumpList::get(int i) const {
    if (i < 0 || head_ == nullptr) {
        return "";
    }

    Node* currentNode = head_;
    int currentIndex = 0;
    
    while (currentNode->jump_ != nullptr && (currentIndex + currentNode->gap_) <= i) {
        currentIndex += currentNode->gap_;
        currentNode = currentNode->jump_;
    }
    
    while (currentNode != nullptr && currentIndex < i) {
        currentNode = currentNode->next_;
        currentIndex++;
    }
    
    return (currentNode != nullptr && currentIndex == i) ? currentNode->data_ : "";
}

string JumpList::print() const {
    string resultString;
    Node* currentNode = head_;

    while (currentNode != nullptr) {
        resultString += currentNode->data_;
        if (currentNode->next_ != nullptr) {
            resultString += " ";
        }
        currentNode = currentNode->next_;
    }
    resultString += "\n";
    
    currentNode = head_;
    bool firstJumpNode = true;
    while (currentNode != nullptr) {
        if (currentNode->gap_ > 0) {
            if (!firstJumpNode) {
                resultString += " ";
            }
            resultString += currentNode->data_;
            firstJumpNode = false;
        }
        currentNode = currentNode->next_;
    }
    resultString += "\n";

    currentNode = head_;
    bool firstGapValue = true;
    while (currentNode != nullptr) {
        if (currentNode->gap_ > 0) {
            if (!firstGapValue) {
                resultString += " ";
            }
            resultString += std::to_string(currentNode->gap_);
            firstGapValue = false;
        }
        currentNode = currentNode->next_;
    }
    
    return resultString;
}

string JumpList::prettyPrint() const {
	// IMPLEMENT ME

	return "nah"; // dummy
}

bool JumpList::insert(const string& stringToInsert) {
    if (head_ == nullptr) {
        head_ = new Node(stringToInsert, nullptr, nullptr, 1);
        return true;
    }
    
    if (stringToInsert < head_->data_) {
        Node* newNode = new Node(stringToInsert, head_, nullptr, head_->gap_ + 1);
        head_->gap_ = 0;
        head_ = newNode;
        return true;
    }
    
    Node* prevNode = nullptr;
    Node* currentNode = head_;
    Node* currentJumpNode = head_;
    Node* nextJumpNode = head_->jump_;
    
    while (currentNode->jump_ != nullptr && currentNode->jump_->data_ < stringToInsert) {
        currentJumpNode = currentNode;
        currentNode = currentNode->jump_;
    }
    
    while (currentNode != nullptr && currentNode->data_ < stringToInsert) {
        prevNode = currentNode;
        currentNode = currentNode->next_;
    }
    
    if (currentNode != nullptr && currentNode->data_ == stringToInsert) {
        return false;
    }
    
    Node* newNode = new Node(stringToInsert, currentNode, nullptr, 0);
    prevNode->next_ = newNode;
    
    int totalNodes = 1;
    Node* counterNode = head_;
    while (counterNode != nullptr) {
        totalNodes++;
        counterNode = counterNode->next_;
    }
    totalNodes--;

    if (totalNodes <= MAX_GAP_SIZE) {
        head_->gap_ = totalNodes;
        head_->jump_ = nullptr;
        return true;
    }
    
    while (nextJumpNode != nullptr && nextJumpNode->data_ <= newNode->data_) {
        currentJumpNode = nextJumpNode;
        nextJumpNode = nextJumpNode->jump_;
    }
    
    currentJumpNode->gap_++;
    
    if (currentJumpNode->gap_ > MAX_GAP_SIZE) {
        int segmentSize = currentJumpNode->gap_;
        int firstSegmentSize = segmentSize / 2;
        if (segmentSize % 2 == 1) {
            firstSegmentSize++; 
        }
        
        Node* splitNode = currentJumpNode;
        for (int i = 1; i < firstSegmentSize; i++) {
            splitNode = splitNode->next_;
        }
        
        Node* newJumpNode = splitNode->next_;
        newJumpNode->gap_ = segmentSize - firstSegmentSize;
        newJumpNode->jump_ = nextJumpNode;
        
        currentJumpNode->gap_ = firstSegmentSize;
        currentJumpNode->jump_ = newJumpNode;
    }
    
    return true;
}



bool JumpList::erase(const string& stringToErase) {
    if (head_ == nullptr) {
        return false;
    }
    
    if (head_->data_ == stringToErase) {
        Node* nodeToDelete = head_;
        head_ = head_->next_;

        if (head_ == nullptr) {
            delete nodeToDelete;
            return true;
        }
        
        if (head_->gap_ == 0) {
            head_->gap_ = nodeToDelete->gap_ - 1;
            head_->jump_ = nodeToDelete->jump_;
        }
        
        delete nodeToDelete;
        return true;
    }
    
    Node* prevNode = nullptr;
    Node* currentNode = head_;
    Node* currentJumpNode = head_;
    
    while (currentNode->jump_ != nullptr && 
           currentNode->jump_->data_ < stringToErase) {
        currentJumpNode = currentNode;  
        currentNode = currentNode->jump_;  
    }
    
    prevNode = currentNode;
    currentNode = currentNode->next_;
    while (currentNode != nullptr && currentNode->data_ < stringToErase) {
        prevNode = currentNode;
        currentNode = currentNode->next_;
    }
    
    if (currentNode == nullptr || currentNode->data_ != stringToErase) {
        return false;
    }
    
    bool isJumpNode = (currentNode->gap_ > 0);
    Node* nextNode = currentNode->next_;
    
    if (!isJumpNode) {
        prevNode->next_ = nextNode;
        
        Node* segmentStartNode = head_;
        while (segmentStartNode != nullptr && segmentStartNode->jump_ != nullptr && 
               segmentStartNode->jump_->data_ <= currentNode->data_) {
            segmentStartNode = segmentStartNode->jump_;
        }
        segmentStartNode->gap_--;
        delete currentNode;
    } else {
        prevNode->next_ = nextNode;
        
        Node* prevJumpNode = head_;
        while (prevJumpNode->jump_ != currentNode) {
            prevJumpNode = prevJumpNode->jump_;
        }
        
        int newGapSize = 0;
        Node* counterNode = prevJumpNode;
        while (counterNode != currentNode->jump_) {
            newGapSize++;
            counterNode = counterNode->next_;
        }
        
        prevJumpNode->jump_ = currentNode->jump_;
        delete currentNode;
        
        if (newGapSize > MAX_GAP_SIZE) {
            int firstSegmentSize = (newGapSize + 1) / 2;
            
            Node* splitNode = prevJumpNode;
            for (int i = 1; i < firstSegmentSize; i++) {
                splitNode = splitNode->next_;
            }
            
            Node* newJumpNode = splitNode->next_;
            newJumpNode->gap_ = newGapSize - firstSegmentSize;
            newJumpNode->jump_ = prevJumpNode->jump_;
            
            prevJumpNode->gap_ = firstSegmentSize;
            prevJumpNode->jump_ = newJumpNode;
        } else {
            prevJumpNode->gap_ = newGapSize;
        }
    }
    
    return true;
}
