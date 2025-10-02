#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <string>
#include <iostream>
#include <map>
#include <shared_mutex>
#include <thread>
#include <utility>

// Edit the methods in the section marked within the UserIPRegistration structure
// And you may add any additional support functions/methods you require!

std::atomic<std::size_t> usersLoggedIn;

struct IPAddress{
	std::size_t address;
};

struct User{
	std::string username;
	int code;
	bool operator==(const User& other) const noexcept { return (this->code == other.code) && (this->username == other.username); }
	std::strong_ordering operator<=>(const User& other) const noexcept {
		if(std::strong_ordering cmp = (this->code <=> other.code); cmp != 0){ return cmp; }
		return username <=> other.username;
	}
};

template<>
struct std::hash<User> {
	size_t operator()(const User& user) {
		return std::hash<std::string>()(user.username);
	}
};

// K: type of the keys being used to access the storage structure (User)
// S: data structure used to store the users -> IPs (std::map<User, IPAddress>)
// M: mutex type used to protect the structure (std::shared_mutex)
template <typename K, typename S, typename M> struct UserIPRegistration{
	// Storage object, used to return the storage from UserIPRegistration using RAII. The mutex will be acquired upon construction and released on destruction.
	// At least your teammate got this part right!
    template <typename Sm, typename L> struct Storage{
		// Constructor the Storage object. Acquires mutex lock of the S storage upon construction and releases on destruction.
		Storage(Sm& storage, M& mutex) : storage(storage), lock(mutex){};
		// Access overload to allow direct reference of the S storage methods.
		Sm* operator->() const{ return &storage; }
		protected:
			// Reference to the S storage associated with this Storage structure. Acessed through the operator->().
			Sm& storage;
			// Lock object of type L. Acquires the lock on construction and releases upon destruction.
			L lock;
	};
	// -------------------------------
	// You may edit anything in here!
	
	// Currently does nothing, because we have no partitions
	UserIPRegistration(std::size_t partitions)
	{
		for (int i = 0; i < partitions; i++)
		{
			_partitions.push_back(std::pair<S, std::shared_ptr<M>>(S(), std::shared_ptr<M>(new M())));
		}
	};
	
	Storage<S, typename std::lock_guard<M>> getStorage(const K& key)
	{
		size_t hash = std::hash<K>()(key);
		size_t index = hash % _partitions.size();
		std::shared_ptr<M> mutex = _partitions[index].second;
		return Storage<S, std::lock_guard<M>>(_partitions[index].first, *mutex);
	}
	
	Storage<const S, typename std::shared_lock<M>> getSharedStorage(const K& key)
	{
		size_t hash = std::hash<K>()(key);
		size_t index = hash % _partitions.size();
		std::shared_ptr<M> mutex = _partitions[index].second;
		return Storage<const S, std::shared_lock<M>>(_partitions[index].first, *mutex);
	}
	
	std::size_t size() const{ 
		size_t total = 0;
		for (int i = 0; i < _partitions.size(); i++)
		{
			total += _partitions[i].first.size();
		}
		return total;
	}
	
	protected:

		std::vector<std::pair<S, std::shared_ptr<M>>> _partitions;
		
	// --------------------------------
};

// Xoshiro256++ by  David Blackman and Sebastiano Vigna, shared under public domain (or equivalent to CC0).
// Source: https://prng.di.unimi.it/
// Paper: David Blackman and Sebastiano Vigna. Scrambled Linear Pseudorandom Number Generators. 2018.
// Date retrieved: 2025/07/21

static inline uint64_t rotl(const uint64_t x, int k){
	return (x << k) | (x >> (64 - k));
}

uint64_t random(uint64_t state[4]) noexcept{
	const uint64_t result = rotl(state[0] + state[3], 23) + state[0];
	const uint64_t t = state[1] << 17;
	state[2] ^= state[0];
	state[3] ^= state[1];
	state[1] ^= state[2];
	state[0] ^= state[3];
	state[2] ^= t;
	state[3] = rotl(state[3], 45);
	return result;
}

std::pair<User,IPAddress> createFakeUser(std::size_t i){
	User newUser;
	uint64_t state[4] = {i, 0, 0 ,0};
	std::size_t namelen = (random(state) % 9) + 5;
	for(std::size_t i = 0; i < namelen; ++i){
		newUser.username += static_cast<char>((random(state) % 26) + 97);
	}
	newUser.code = random(state) % 10000;
	return {newUser, IPAddress{ random(state) & 0xFFFFFFFF }};
}

template <typename T> void simulateLoggins(T& registration, std::size_t logins){
	std::size_t idx;
	while((idx = ++usersLoggedIn) <= logins){
		auto userIP = createFakeUser(idx);
		{
			auto storage = registration.getStorage(userIP.first);
			storage->insert(userIP);
		}
	}
}

void runThreads(std::size_t ts, std::size_t logins){
	std::cout << "Running with " << ts << " threads\n";
	UserIPRegistration<User, std::map<User, IPAddress>, std::shared_mutex> registration(512);
	std::jthread* threads = new std::jthread[ts];
	usersLoggedIn = 0;
	auto start = std::chrono::high_resolution_clock::now();
	for(std::size_t t=0; t < ts; ++t){ threads[t] = std::move(std::jthread(simulateLoggins<UserIPRegistration<User, std::map<User, IPAddress>, std::shared_mutex>>, std::ref(registration), logins));   }
	for(std::size_t t=0; t < ts; ++t){ threads[t].join(); }
	auto end = std::chrono::high_resolution_clock::now();
	float ttaken = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	std::cout << "Time taken for " << logins << " logins with " << ts << " threads: " << ttaken << "ms" << std::endl;
	std::cout << "Total items in registry: " << registration.size() << std::endl;
	delete [] threads;
}

int main(){
	runThreads(1, 8*1024*1024);
	runThreads(4, 8*1024*1024);
    std::cout << "Press ENTER to exit...";
    fgetc(stdin);
    return 0;
}
