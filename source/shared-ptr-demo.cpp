#include <iostream>
#include <atomic>
// just to add link/runtime dependency on boost
#include <boost/filesystem.hpp>
#include <boost/atomic.hpp>

// just dummy class with logged constructors/operators
struct Data {
    Data() { std::cout << "Default ConstructorData\n"; }
    Data(int x, int y, int z) : _x(x), _y(y), _z(z) { std::cout << "ConstructorData\n"; }
    Data(const Data& other) { _x = other._x; _y = other._y; _z = other._z; std::cout << "Copy ConstructorData\n"; }
    Data(Data&& other) noexcept { _x = other._x; _y = other._y; _z = other._z; std::cout << "Move ConstructorData\n"; }
    Data& operator=(const Data& other) {
        std::cout << "Copy AssignmentData\n";
        if (this != &other) { _x = other._x; _y = other._y; _z = other._z; }
        return *this;
    }
    Data& operator=(Data&& other) noexcept {
        std::cout << "Move AssignmentData\n";
        if (this != &other) { _x = other._x; _y = other._y; _z = other._z; }
        return *this;
    }
    ~Data() { std::cout << "~DestructorData\n"; }

    friend std::ostream& operator<< (std::ostream& os, const Data& d);

    int _x{}, _y{}, _z{};
};

std::ostream& operator<< (std::ostream& os, const Data& d)
{
    std::cout << "x: " << d._x << ", y: " << d._y << ", z: " << d._z << std::endl;
    return os;
}

template<typename T>
class MyWeakPtr; // forward declaration

// Just a tag to mark we are taking existing control block,
// e.g. from already existing shared_ptr.
struct FromOtherTag {};
constexpr FromOtherTag from_other_tag{};


// ***** SharedPtr Simplified Implementation *****

// Base class for the Control Block to allow polymorphic deletion
// (it allows us to have multiple child Control Blocks with different
// implementations, but store one pointer to Base Control Block in shared_ptr class).
struct ControlBlock {
    // only increment/decrement count is thread safe. Accessing data - not!!!
    // e.g. if 2 threads delete 2 shared_ptr pointing to same CB simultaneously,
    // destructor will be called only once (ok).
    // if we read/copy same shared_ptr from few threads (ok).
    // if at least 1 thread write/assign to same shared ptr (NOT ok)!
    // (e.g., ptr1 = ptr3; ptr2 = ptr1; -> "Frankenstein pointer", Data Race
    // -> data from ptr3 but control block from ptr1, causing UB and Segfaults).
    // Conclusion: ControlBlock is thread-safe(atomic counts), but SharedPtr object itself and its data - not!
    boost::atomic<int> shared_count{ 1 };
    boost::atomic<int> weak_count{ 0 };

    // virtual destructor for proper deletion of CB itself
    virtual ~ControlBlock() = default;

    // Abstract method to destroy the managed object without knowing its type here
    virtual void dispose() = 0;
};

// Derived Control Block that knows the specific type T
// It's a version used when we pass 'new T' to shared_ptr constructor.
template<typename T>
struct ControlBlockPtr : public ControlBlock {
    // Holding T* inside both ControlBlock and SharedPtr helps to implement
    // aliasing constructor (see comments after main) and simplifies access.
    T* ptr;

    ControlBlockPtr(T* p) : ptr(p) {}

    // Concrete implementation of object destruction
    void dispose() override {
        std::cout << "[ControlBlockPtr] dispose(). Deleting managed object via 'delete'...\n";
        delete ptr;
    }
};

template<typename T>
class MySharedPtr {
private:
    T* ptr = nullptr;       // Pointer to the managed object (for fast access/aliasing)
    ControlBlock* cb = nullptr; // Pointer to the shared metadata (ref counts)
    // two pointers - size of shared_ptr is 16 bytes.

    // INTERNAL CONSTRUCTOR: Used by make_shared
    // We make it private to prevent users from accidentally passing 
    // wrong pointers or unmanaged control blocks.
    // This is a newly created cb by make_shared, no need of increment.
    MySharedPtr(T* p, ControlBlock* c) : ptr(p), cb(c) {
        std::cout << "[SharedPtr] Internal constructor (from make_shared): shared_count now " << cb->shared_count << "\n";
    }

    // This is already existing cb from existing shared_ptr, and we are taking
    // it to create another shared_ptr, so we need to increment shared_count.
    MySharedPtr(T* p, ControlBlock* c, FromOtherTag) : ptr(p), cb(c) {
        if (cb) {
            cb->shared_count++;
            std::cout << "[SharedPtr] Internal constructor (from weak_ptr): shared_count now " << cb->shared_count << "\n";
        }
    }
    // P.s. Better to have 2 constructors (one without tag, one with tag) than one
    // constructor with bool flag and runtime if inside.
    // (e.g. MySharedPtr(T* p, ControlBlock* c, bool from_other) { if (from_other) ....) 
    // Because instead of checking condition for each constructor call compiler
    // will insert proper constructor at compile time.
    // It's zero cost (do not pay for what you do not use).

    // We need to make my_make_shared and MyWeakPtr a friend to access the private constructor
    template<typename U, typename... Args>
    friend MySharedPtr<U> my_make_shared(Args&&... args);
    template<typename V>
    friend class MyWeakPtr;
public:
    // As next one constructor is templatized, type can't be deduced when we pass nullptr.
    // Constructor with nullptr solves deduction problem.
    MySharedPtr(std::nullptr_t) : ptr(nullptr), cb(nullptr) {
        std::cout << "[SharedPtr] Constructor from nullptr.\n";
    }

    // Constructor for explicit allocation: MySharedPtr<Data> p(new Data());
    template<typename Y>
    explicit MySharedPtr(Y* p) : ptr(p) {
        if (p) {
            try {
                // In a real STL, this is where the second heap allocation happens
                cb = new ControlBlockPtr<Y>(p);
                std::cout << "[SharedPtr] Template Constructor. Control Block allocated separately.  shared_count now " << cb->shared_count << "\n";
            }
            catch (const std::exception& e)
            {
                std::cerr << "[SharedPtr] Critical error during CB allocation: " << e.what() << "\n";
                delete p; // avoid memory leak of data object in case CB allocation failed
                throw;
            }
        }
        else
            std::cout << "[SharedPtr] Template Constructor from NULL(old style)!\n";
    }
    // MAGIC: We create a block for type Y (Derived),
    // even if SharedPtr itself is of type T (Base)!

    // Copy Constructor: Increments the shared reference count
    MySharedPtr(const MySharedPtr& other) : ptr(other.ptr), cb(other.cb) {
        if (cb) {
            cb->shared_count++;
            std::cout << "[SharedPtr] CopyConstrucotor.  shared_count incremented: " << cb->shared_count << "\n";
        }
    }

    // Move Constructor: Transfers ownership without changing reference counts
    MySharedPtr(MySharedPtr&& other) noexcept : ptr(other.ptr), cb(other.cb) {
        other.ptr = nullptr;
        other.cb = nullptr;
        std::cout << "[SharedPtr] Ownership transferred via Move.  shared_count now " << cb->shared_count << "\n";
    }

    ~MySharedPtr() {
        std::cout << "~MySharedPtr(): ";
        release();
    }

    // Сopy Assignment operator using the "copy-and-swap" idiom or manual release
    MySharedPtr& operator=(const MySharedPtr& other) {
        if (this != &other) {
            release(); // Drop current ownership
            ptr = other.ptr;
            cb = other.cb;
            if (cb) cb->shared_count++;
            std::cout << "[SharedPtr] CopyAssignment.  shared_count now " << cb->shared_count << "\n";
        }
        return *this;
    }

    // Move Assignment operator
    MySharedPtr& operator=(MySharedPtr&& other) noexcept {
        if (this != &other) {
            release();
            ptr = other.ptr;
            cb = other.cb;
            other.ptr = nullptr;
            other.cb = nullptr;
            std::cout << "[SharedPtr] MoveAssignment. shared_count unchanged.\n";
        }
        return *this;
    }

    int use_count() const {
        return cb ? cb->shared_count.load() : 0;
    }

    T* operator->() const { assert(ptr != nullptr); return ptr; }
    T& operator*() const { assert(ptr != nullptr); return *ptr; }

private:
    void release() {
        if (cb) {
            // Atomic decrement and check if we are the last owner
            if (--cb->shared_count == 0) {
                cb->dispose(); // Object T is destroyed here
                std::cout << "[SharedPtr] release().  shared_count is 0. Object destroyed.\n";

                // Only delete the Control Block if no weak_ptrs are watching
                if (cb->weak_count == 0) {
                    delete cb; // cb is alive while shared_count + weak_count > 0
                    std::cout << "[SharedPtr] release().  No weak pointers. Control Block deleted.\n";
                }
            }
            else
                std::cout << "[SharedPtr] release(). shared_count " << cb->shared_count << "\n";
        }
        else
            std::cout << "[SharedPtr] release(). Empty shared_ptr (nullptr)\n";
    }
};

// ***** SharedPtr Simplified Implementation *****



// ***** make_shared idea *****

// 1. Special Control Block that HOLDS the object inside itself
template<typename T>
struct ControlBlockForMakeShared : public ControlBlock {
    // alignas ensures the memory is correctly aligned for type T
    // We use a raw buffer (storage) instead of a direct object member 
    // to control EXACTLY when the constructor and destructor are called.
    alignas(T) char storage[sizeof(T)];

    template<typename... Args>
    ControlBlockForMakeShared(Args&&... args) {
        // PLACEMENT NEW: Construct the object directly in our internal buffer
        new (storage) T(std::forward<Args>(args)...);
    }

    // In case of make_shared usage cb actually owns the object.
    // Returning T* simplifies access to the oject itself.
    T* get_ptr() { return reinterpret_cast<T*>(storage); }

    void dispose() override {
        std::cout << "[ControlBlockForMakeShared] dispose(). Manually calling destructor of T...\n";
        // Call destructor manually because T was created via placement new
        get_ptr()->~T();
    }

    // NOTE: The memory of the ControlBlock itself (including 'storage') 
    // will be deleted only when shared_count AND weak_count both reach zero.
    // This is a disadvantage of make_shared - if there are at least one weak_ptr
    // to an object - memory for it will remain in ControlBlock, even though
    // the destructor ~T has already been called (when shared_count became 0)!
    // 
    // When we create shared_ptr directly with 'new', the memory for object
    // is deleted as soon as shared_count is 0.
};

// Instead of two calls to the heap (new T and new ControlBlock), we perform only one.
// Not only we reduces the number of calls to new/delete, but also force object and
// control block to be side by side in memory, reduces chances of cash miss.
template<typename T, typename... Args>
MySharedPtr<T> my_make_shared(Args&&... args) {
    std::cout << "my_make_shared:   ";
    // 1. ONE SINGLE ALLOCATION for both Control Block and T
    auto* cb = new ControlBlockForMakeShared<T>(std::forward<Args>(args)...);

    // 2. Call the private "internal" constructor.
    // We return a SharedPtr that points to the object INSIDE the control block
    return MySharedPtr<T>(cb->get_ptr(), cb);
}

// ***** make_shared idea *****



// ***** WeakPtr Simplified Implementation *****

template<typename T>
class MyWeakPtr {
private:
    T* ptr = nullptr;
    ControlBlock* cb = nullptr;

    void release() {
        if (cb) {
            // Decrement weak_count and check if this was the last link to the Control Block
            if (--cb->weak_count == 0 && cb->shared_count == 0) {
                std::cout << "[WeakPtr] release(): Last weak reference gone. Deleting Control Block.\n";
                delete cb;
            }
            cb = nullptr;
            ptr = nullptr;
        }
    }
public:
    MyWeakPtr() = default;

    // Create a weak_ptr from a shared_ptr
    MyWeakPtr(const MySharedPtr<T>& sptr) : ptr(sptr.ptr), cb(sptr.cb) {
        if (cb) cb->weak_count++;
        std::cout << "[WeakPtr] Created from shared_ptr. weak_count: " << (cb ? cb->weak_count.load() : 0) << "\n";
    }

    // 1. Copy Constructor
    MyWeakPtr(const MyWeakPtr& other) : ptr(other.ptr), cb(other.cb) {
        if (cb) cb->weak_count++;
        std::cout << "[WeakPtr] Copied. weak_count: " << (cb ? cb->weak_count.load() : 0) << "\n";
    }

    // 2. Copy Assignment Operator
    MyWeakPtr& operator=(const MyWeakPtr& other) {
        if (this != &other) {
            release(); // Clean up current association
            ptr = other.ptr;
            cb = other.cb;
            if (cb) cb->weak_count++;
            std::cout << "[WeakPtr] Assigned from other weak_ptr. weak_count: " << (cb ? cb->weak_count.load() : 0) << "\n";
        }
        return *this;
    }

    // 3. Assignment from SharedPtr (for Case: wptr = some_shared_ptr)
    MyWeakPtr& operator=(const MySharedPtr<T>& sptr) {
        release();
        ptr = sptr.ptr;
        cb = sptr.cb;
        if (cb) cb->weak_count++;
        std::cout << "[WeakPtr] Assigned from other shared_ptr. weak_count: " << (cb ? cb->weak_count.load() : 0) << "\n";
        return *this;
    }

    // 4. Move Constructor
    MyWeakPtr(MyWeakPtr&& other) noexcept : ptr(other.ptr), cb(other.cb) {
        other.ptr = nullptr;
        other.cb = nullptr;
        std::cout << "[WeakPtr] Moved from other weak_ptr. weak_count: " << (cb ? cb->weak_count.load() : 0) << "\n";
        // No need to change weak_count, we just took over the existing one
    }

    ~MyWeakPtr() {
        std::cout << "~MyWeakPtr(): ";
        release();
    }

    // The key method: tries to "resurrect" the shared_ptr
    MySharedPtr<T> lock() const {
        std::cout << "[WeakPtr] lock(). shared_count " << (cb ? cb->shared_count.load() : 0) << "\n";
        if (cb && cb->shared_count > 0) {
            // In real STL, this must be an atomic increment-if-not-zero
            // (to prevent lock attempt while another thread is deleting shared_ptr,
            // this may cause race condition - other thread may decrement counter and
            // delete the object between our check and increment operations).
            return MySharedPtr<T>(ptr, cb, from_other_tag);
            // special internal contructor (when we take cb from existing shrared_ptr).
            // It will increment shared_count.
        }
        return MySharedPtr<T>(nullptr);
    }
    // 1) Parent cb already created with shared_count==1.
    // 2) Constructor of shared_ptr taking raw pointer creates new cb inside, also with shared_count==1, ok.
    // 3) make_shared also creates new cb, shared_count==1, usual internal contructor just assigning pointers
    // and do not increment, ok.
    // 4) But lock creates shared_ptr passing already existing cb from original shared_ptr,
    // that's why it needs special internal constructor that should inrement shared_count.

    bool expired() const {
        return cb && cb->shared_count > 0 ? false : true;
    }
};

// ***** WeakPtr Simplified Implementation *****



void TestMySharedPtr()
{
    MySharedPtr<Data> ptr{ new Data(1, 2, 3) };
    std::cout << "ptr->_x:\t" << ptr->_x << "\n";
    std::cout << "*ptr:\t" << *ptr;

    std::cout << "\n###\n";
    {
        MySharedPtr<Data> ptr2 = ptr;
    }
    std::cout << "###\n\n";
}

void TestMySharedPtrMake()
{
    MySharedPtr<Data> ptr = my_make_shared<Data>(1, 2, 3);
    std::cout << "ptr->_x:\t" << ptr->_x << "\n";
    std::cout << "*ptr:\t" << *ptr;

    std::cout << "\n###\n";
    {
        MySharedPtr<Data> ptr2 = ptr;
    }
    std::cout << "###\n\n";
}

void TestMyWeakPtr()
{
    MyWeakPtr<Data> wptr;
    std::cout << "\n###\n";
    {
        MySharedPtr<Data> ptr = my_make_shared<Data>(1, 2, 3);
        wptr = ptr; // initialize weak_ptr
        MySharedPtr ptr2 = wptr.lock(); // get shared_ptr from weak_ptr
        std::cout << "ptr->_x:\t" << ptr->_x << "\n";
        std::cout << "*ptr:\t" << *ptr;
    }
    std::cout << "###\n\n";

    MySharedPtr ptr3 = wptr.lock(); // gives empty shared_ptr
    std::cout << "ptr3.use_count():\t" << ptr3.use_count() << "\n";
    if (wptr.expired())
        std::cout << "wptr is expired!\n";
}

void TestBaseSharedDerivedObj()
{
    struct Base {
        Base() { std::cout << "Base()\n"; }
        // note: no virtual destructor here!
        ~Base() { std::cout << "~Base()\n"; }
    };
    struct Derived : public Base {
        Derived() { std::cout << "Derived()\n"; }
        ~Derived() { std::cout << "~Derived()\n"; }
    };

    // shared_ptr allows to write like that:
    MySharedPtr<Base> sptr{ new Derived{} };
    // even if Base has no virtual destructor, object still will be deleted correctly,
    // because control block of shared_ptr will remember 'Derived' type of owned object.
}

int main()
{
    // to add link/runtime dependency on boost
    boost::filesystem::path p = boost::filesystem::current_path();
    std::cout << "\n***This test happens at:  " << p << "\n\n";

    std::cout << "***** TestMySharedPtr *****\n";
    TestMySharedPtr();
    std::cout << "***** TestMySharedPtr *****\n\n";

    std::cout << "***** TestMySharedPtrMake *****\n";
    TestMySharedPtrMake();
    std::cout << "***** TestMySharedPtrMake *****\n\n";

    std::cout << "***** TestMyWeakPtr *****\n";
    TestMyWeakPtr();
    std::cout << "***** TestMyWeakPtr *****\n\n";

    std::cout << "***** TestBaseSharedDerivedObj *****\n";
    TestBaseSharedDerivedObj();
    std::cout << "***** TestBaseSharedDerivedObj *****\n\n";
}

// 1. Aliasing constructor of shared_ptr
// This is why both ControlBlock and SharedPtr contains T*
// It allow initialize shared_ptr with control_block of shared_ptr to one object,
// but make T* of new shared_ptr point to another object (e.g. member of object stored in original shared_ptr).
// Ref_count will be incremented to 2, but managable object will be the one inside control block (from original shared_ptr).
// And only this object will be deleted when ref_count becomes 0,
// nothing will be done with another object (its validity/lifetime/deletion - is responsibility of a user).
// This is another reason why ControlBlock holds T* - it is responsible for deletion at last.
/*
How it Works:
A shared_ptr conceptually consists of two parts:

The owned pointer (control block):
The actual object whose lifetime is managed by the reference count. When the reference count drops to zero, this object is deleted.

The stored pointer:
The pointer that the shared_ptr actually holds and to which it delegates operations like get(), operator*, and operator->.

In most shared_ptr constructors, the owned and stored pointers are the same.
The aliasing constructor breaks this link, using the first argument to determine the owned object
and the second argument to determine the stored (aliased) object.

The signature of the aliasing constructor is:

template< class Y >
shared_ptr( const shared_ptr<Y>& r, element_type* ptr ) noexcept; // C++11
// An rvalue overload was added in C++20

r: A shared_ptr that owns the object whose lifetime is to be shared. The new shared_ptr will increment the reference count of r's managed object.
ptr: A raw pointer to the object that the new shared_ptr will store and point to.
This pointer is not managed by the new shared_ptr's deleter; its validity must be ensured by the user.

Example:

struct Person {
    int age;
    std::string name;
};

int main() {
    // 1. Create a shared_ptr to the entire Person object
    std::shared_ptr<Person> alice = std::make_shared<Person>();
    alice->age = 38;
    alice->name = "Alice";

    // 2. Create an aliased shared_ptr to the 'name' member
    std::shared_ptr<std::string> name_ptr(alice, &alice->name);
    // ^ The first argument ensures 'alice' stays alive

    // 'alice' goes out of scope, but the Person object is NOT deleted
    // because 'name_ptr' still holds a shared ownership reference.
    alice.reset();

    // This way, if we don't want/need to access whole object any longer,
    // but want to acceess its part - we can do it. Only this part is available now.
    // But whole object still is owned and will be properly deleted at the end.
    std::cout << "Name is: " << *name_ptr << std::endl; // Valid dereference

    // When 'name_ptr' goes out of scope, the Person object is finally deleted.

    return 0;
}

Important notes:

Lifetime Management: 
The user must ensure that the aliased pointer (ptr) remains valid as long as the owned object (r) is alive.
In the example above, this is guaranteed because &alice->name is a pointer to a member of the owned object.

Dangling Pointers:
If the aliased pointer points to an unrelated object or one with a shorter lifetime, you risk a dangling pointer.

Readability:
The usage can be somewhat opaque and might require careful code review to ensure correct lifetime management.
*/
