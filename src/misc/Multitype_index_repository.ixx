//
// Created by A on 19.04.2026.
//
module;

#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <iterator>
#include <set>

export module multitype_index_repository;

export
template <typename repoClass>
struct repository
{
public:
    struct signedContainer
    {
        std::string sDesc;
        unsigned int iDesc;
        repoClass object;
    };

    repository(size_t reserve = 256)
    {
        sDescToIndex.reserve(reserve);
        iDescToIndex.reserve(reserve);
        signedContainers.reserve(reserve);
    }

private:
    std::unordered_map<std::string,  unsigned int> sDescToIndex;
    std::unordered_map<unsigned int, unsigned int> iDescToIndex;
    std::vector<signedContainer> signedContainers;
    unsigned int lastDesc = 0;

public:
    signedContainer& operator[](const size_t& desc)
    {
        return signedContainers.at(iDescToIndex.at(desc));
    }
    signedContainer& operator[](const std::string& desc)
    {
        return signedContainers.at(sDescToIndex.at(desc));
    }
    bool contains(const size_t& desc)
    {
        return iDescToIndex.contains(desc);
    }
    bool contains(const std::string& desc)
    {
        return sDescToIndex.contains(desc);
    }

    size_t add(const std::string& sDesc, const repoClass& object)
    {
        signedContainers.emplace_back(signedContainer(sDesc, lastDesc, object));
        size_t index = signedContainers.size() - 1;
        sDescToIndex.emplace(sDesc, index);
        iDescToIndex.emplace(lastDesc, index);
        return lastDesc++;
    }

    std::vector<unsigned int> iDescs()
    {
        std::vector<unsigned int> keys(iDescToIndex.size());
        std::transform(iDescToIndex.begin(), iDescToIndex.end(), keys.begin(),
                       [](const auto& pair) { return pair.first; });
        return keys;
    }

    unsigned int get_id(const std::string& sDesc) const {
        auto it = sDescToIndex.find(sDesc);
        if (it != sDescToIndex.end()) {
            return it->second; // Это индекс в векторе, но нам нужен iDesc (ключ в iDescToIndex)
            // Стоп. В твоей реализации:
            // sDescToIndex maps string -> index in vector
            // iDescToIndex maps iDesc (lastDesc) -> index in vector
            // signedContainers[index].iDesc is the actual ID used externally.

            size_t vecIndex = it->second;
            return signedContainers[vecIndex].iDesc;
        }
        throw std::runtime_error("Descriptor not found");
    }
};