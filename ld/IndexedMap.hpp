#ifndef IndexedMap_hpp
#define IndexedMap_hpp

#import <vector>

template <class T>
class IndexedMap {
    T fTombstone;
    T fDefault;
    std::vector<T> fValues;
public:
    IndexedMap(T tombstone, T aDefault) : fTombstone(tombstone), fDefault(aDefault) {}

    void erase(size_t key) {
        if (key >= fValues.size()) {
            return;
        }
        fValues[key] = fTombstone;
    }

    void setValue(size_t key, T value) {
        if (key == fValues.size()) {
            fValues.push_back(value);
            return;
        }
        size_t previousSize = fValues.size();
        if (fValues.size() <= key) {
            fValues.resize(key + 1);
            std::fill_n(fValues.begin() + previousSize, fValues.size() - previousSize - 1, fTombstone);
        }
        fValues[key] = value;
    }

    T getValueOrDefault(size_t key) {
        if (key >= fValues.size()) {
            setValue(key, fDefault);
            return fDefault;
        }
        T value = fValues[key];
        return value == fTombstone ? fDefault : value;
    }

    T getValue(size_t key, bool *doesExist) const {
        T value = fValues[key];
        *doesExist = value != fTombstone;
        return value;
    }
};

#endif /* IndexedMap_hpp */
