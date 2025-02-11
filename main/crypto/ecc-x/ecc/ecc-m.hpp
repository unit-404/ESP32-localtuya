#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

// Для простоты определим типы для 256‐битового и 288‐битового числа как:
using U256 = std::array<uint32_t, 8>;
using U288 = std::array<uint32_t, 9>;
using Bytes = std::vector<uint8_t>;

// Функция zeroize – гарантированное обнуление памяти (не оптимизируется компилятором)
inline void zeroize(void* d, size_t n) {
    volatile uint8_t* p = reinterpret_cast<volatile uint8_t*>(d);
    while(n--) *p++ = 0;
}

//
namespace p256_m {

    //─────────────────────────────────────────────────────────────
    // 1. Операции над 256‐битовыми числами (представленными массивом из 8 uint32_t, limbs в порядке LSF)
    //─────────────────────────────────────────────────────────────

    // Установить U256 в значение x (где x < 2^32)
    inline void u256_set32(U256 &z, uint32_t x) {
        z[0] = x;
        for (size_t i = 1; i < z.size(); i++)
            z[i] = 0;
    }

    // 256-битное сложение: z = x + y (с возвращением переноса – 0 или 1)
    inline uint32_t u256_add(U256 &z, const U256 &x, const U256 &y) {
        uint64_t sum = 0;
        uint32_t carry = 0;
        for (size_t i = 0; i < z.size(); i++) {
            sum = static_cast<uint64_t>(x[i]) + y[i] + carry;
            z[i] = static_cast<uint32_t>(sum);
            carry = static_cast<uint32_t>(sum >> 32);
        }
        return carry;
    }

    // 256-битное вычитание: z = x - y, возвращает 0 если x>=y, 1 если x<y.
    inline uint32_t u256_sub(U256 &z, const U256 &x, const U256 &y) {
        uint64_t diff = 0;
        uint32_t carry = 0;
        for (size_t i = 0; i < z.size(); i++) {
            diff = static_cast<uint64_t>(x[i]) - y[i] - carry;
            z[i] = static_cast<uint32_t>(diff);
            // Если diff отрицательный, старшая часть будет все единицы
            carry = (diff >> 63) & 1; // так как если diff отрицательный, (diff>>63)==1
        }
        return carry;
    }

    // Условное присваивание: если c == 1, то z = x; иначе z остаётся неизменным.
    inline void u256_cmov(U256 &z, const U256 &x, uint32_t c) {
        uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(c)); // либо 0 или 0xFFFFFFFF
        for (size_t i = 0; i < z.size(); i++) {
            z[i] = (z[i] & ~mask) | (x[i] & mask);
        }
    }

    // Функция сравнения: возвращает 0 если x == y, иначе ненулевое значение.
    inline uint32_t u256_diff(const U256 &x, const U256 &y) {
        uint32_t diff = 0;
        for (size_t i = 0; i < x.size(); i++) {
            diff |= (x[i] ^ y[i]);
        }
        return diff;
    }

    // Проверка числа на 0: возвращает 0 если x == 0, иначе ненулевое.
    inline uint32_t u256_diff0(const U256 &x) {
        uint32_t diff = 0;
        for (auto limb: x)
            diff |= limb;
        return diff;
    }

    //─────────────────────────────────────────────────────────────
    // 2. Умножение и накопление 32x32->64 бит
    // Реализация без ассемблера – fallback вариант.
    inline uint64_t u32_muladd64(uint32_t x, uint32_t y, uint32_t z, uint32_t t) {
        return static_cast<uint64_t>(x) * y + z + t;
    }

    //─────────────────────────────────────────────────────────────
    // 3. Специальные операции для 288-битовых чисел
    // 288-битовое умножение с накоплением: вычисляет z = z + x * y, где z имеет 9 limbs.
    inline uint32_t u288_muladd(U288 &z, uint32_t x, const U256 &y) {
        uint32_t carry = 0;
        for (size_t i = 0; i < y.size(); i++) {
            uint64_t prod = u32_muladd64(x, y[i], z[i], carry);
            z[i] = static_cast<uint32_t>(prod);
            carry = static_cast<uint32_t>(prod >> 32);
        }
        uint64_t sum = static_cast<uint64_t>(z[8]) + carry;
        z[8] = static_cast<uint32_t>(sum);
        carry = static_cast<uint32_t>(sum >> 32);
        return carry;
    }

    // Правый сдвиг 288-битового числа на 32 бита (с добавлением c в старший limb)
    inline void u288_rshift32(U288 &z, uint32_t c) {
        for (size_t i = 0; i < z.size()-1; i++)
            z[i] = z[i+1];
        z[8] = c;
    }

    //─────────────────────────────────────────────────────────────
    // 4. Импорт/экспорт 256-битового числа в виде big-endian байтов.
    // Преобразование из 32-байтового массива (big-endian) в U256:
    inline void u256_from_bytes(U256 &z, const uint8_t p[32]) {
        for (size_t i = 0; i < z.size(); i++) {
            size_t j = 4 * (7 - i);
            z[i] = (static_cast<uint32_t>(p[j]) << 24) |
                   (static_cast<uint32_t>(p[j+1]) << 16) |
                   (static_cast<uint32_t>(p[j+2]) << 8) |
                   (static_cast<uint32_t>(p[j+3]));
        }
    }

    // Экспорт U256 в big-endian массив из 32 байт:
    inline void u256_to_bytes(uint8_t p[32], const U256 &z) {
        for (size_t i = 0; i < z.size(); i++) {
            size_t j = 4 * (7 - i);
            p[j]   = static_cast<uint8_t>(z[i] >> 24);
            p[j+1] = static_cast<uint8_t>(z[i] >> 16);
            p[j+2] = static_cast<uint8_t>(z[i] >> 8);
            p[j+3] = static_cast<uint8_t>(z[i]);
        }
    }

    //─────────────────────────────────────────────────────────────
    // 5. Montgomery операции
    // Структура, содержащая параметры для Montgomery-арифметики:
    struct m256_mod {
        U256 m;   // модуль (например, p или n)
        U256 R2;  // R^2 mod m, где R = 2^256
        uint32_t ni; // -m^-1 mod 2^32
    };

    // Для удобства определим модуль для кривой P-256 (поля) и для порядка n
    static const m256_mod p256_p = {
        /* mod m: представлено в виде 8-limb (LSF first) – значения взяты из C-кода */
        /* Обратите внимание на порядок limbs – здесь мы используем тот же порядок, что и в u256_from_bytes */
        { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0xFFFFFFFF },
        { 0x00000003, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFB, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFD, 0x00000004 },
        0x00000001
    };

    //
    static const m256_mod p256_n = {
        { 0xFC632551, 0xF3B9CAC2, 0xA7179E84, 0xBCE6FAAD, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF },
        { 0xBE79EEA2, 0x83244C95, 0x49BD6FA6, 0x4699799C, 0x2B6BEC59, 0x2845B239, 0xF3D95620, 0x66E12D94 },
        0xEE00BC4F
    };

    // Функция Montgomery-умножения: вычисляет z = (x*y)/R mod m,
    // где входные значения x,y заданы в Montgomery-представлении, а результат также.
    inline void m256_mul(U256 &z, const U256 &x, const U256 &y, const m256_mod &mod) {
        // Реализация алгоритма HAC 14.36.
        uint32_t m_prime = mod.ni;
        U288 a = {}; // 9-элементный аккумулятор, инициализируем нулями.
        for (size_t i = 0; i < 8; i++) {
            // Вычисляем u = (a[0] + x[i]*y[0]) * m_prime mod 2^32
            uint32_t u = static_cast<uint32_t>((a[0] + static_cast<uint64_t>(x[i]) * y[0]) * m_prime);
            // Прибавляем x[i]*y и u*m к аккумулятору a.
            uint32_t c = u288_muladd(a, x[i], y);
            c += u288_muladd(a, u, mod.m);
            u288_rshift32(a, c);
        }
        // Если a >= m, вычесть m.
        U256 tmp;
        uint32_t carry_add = u256_add(tmp, a.data(), mod.m.data());
        uint32_t carry_sub = u256_sub(z, a.data(), mod.m.data());
        uint32_t use_sub = carry_add | (1 - carry_sub);
        // Выполняем условное присваивание.
        u256_cmov(z, tmp, 1 - use_sub);
    }

    // Преобразование числа в Montgomery-область: x -> x*R mod m.
    inline void m256_prep(U256 &z, const m256_mod &mod) {
        m256_mul(z, z, mod.R2, mod);
    }

    // Выход из Montgomery-области: z -> z/R mod m.
    inline void m256_done(U256 &z, const m256_mod &mod) {
        U256 one;
        u256_set32(one, 1);
        m256_mul(z, z, one, mod);
    }

    //─────────────────────────────────────────────────────────────
    // 6. Функции получения/экспорта модульных чисел из байтов
    // Импорт числа из 32 байтов в U256 и перевод в Montgomery-область (возвращает 0, если число в диапазоне [0, m)).
    inline int m256_from_bytes(U256 &z, const uint8_t p[32], const m256_mod &mod) {
        u256_from_bytes(z, p);
        U256 t;
        uint32_t lt_m = u256_sub(t, z, mod.m);
        if(lt_m != 1)
            return -1;
        m256_prep(z, mod);
        return 0;
    }

    inline void m256_to_bytes(uint8_t p[32], const U256 &z, const m256_mod &mod) {
        U256 zi;
        u256_cmov(zi, z, 1); // копирование
        m256_done(zi, mod);
        u256_to_bytes(p, zi);
    }

    //─────────────────────────────────────────────────────────────
    // Дополнительные функции (точечная арифметика, скалярное умножение, ECDH/ECDSA)
    // Здесь приведены только прототипы и основные идеи. Полная реализация требует портирования
    // всех вспомогательных функций из исходного C-кода (point_double, point_add, scalar_mult и т.д.).
    //
    // Например, можно оформить класс для работы с точками кривой P-256:
    struct AffinePoint {
        U256 x;
        U256 y;
    };

    struct JacobianPoint {
        U256 X;
        U256 Y;
        U256 Z;
    };

    // Прототип для преобразования точек из Якобианских координат в аффинные:
    inline AffinePoint point_to_affine(const JacobianPoint &P) {
        // Если Z == 0, трактуем как точку в бесконечности (возвращаем (0,0)).
        if(u256_diff(P.Z) == 0)
            return {{0}, {0}};
        U256 zInv = P.Z;
        zInv = P256_m::m256_inv(zInv, zInv, p256_p); // здесь нужно реализовать инверсию (не приведена полностью)
        // Далее X_affine = X * zInv^2, Y_affine = Y * zInv^3, выполняются Montgomery умножения.
        AffinePoint A;
        // Реализуйте умножение с вызовом m256_mul и m256_done.
        return A;
    }

    // Прототип скалярного умножения: R = s*P (P в аффинном виде)
    // Функция scalar_mult(...) следует портировать по аналогии с исходным кодом.
    inline void scalar_mult(U256 &rx, U256 &ry,
                            const U256 &px, const U256 &py,
                            const U256 &s) {
        // Реализуйте алгоритм лестницы для умножения точки на скаляр.
        // Для демонстрации оставляем заглушку.
        rx = px; ry = py;
    }

    //─────────────────────────────────────────────────────────────
    // Дальнейшие функции для работы с ECDH/ECDSA (импорт/экспорт точек, генерация ключей,
    // ECDSA-сигнатуры и верификация) можно реализовать аналогичным образом, используя функции
    // выше и дописывая защиту, zeroize и преобразования в Montgomery-форму.
    //
    // Обратите внимание, что реализация таких функций требует обеспечения защиты секретных данных,
    // константно-временной арифметики и корректной работы при экспорте/импорте байтовых представлений.
    //─────────────────────────────────────────────────────────────

} // namespace p256_m



// конец p256_m.hpp
