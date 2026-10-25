#ifndef LINN_PIMPL_H_
#define LINN_PIMPL_H_

#include <memory> // std::unique_ptr

#define LINN_DISABLE_COPY(CLASS)             \
  CLASS(CLASS const &)             = delete; \
  CLASS &operator= (CLASS const &) = delete;

#define LINN_DISABLE_MOVE(CLASS)        \
  CLASS(CLASS &&)             = delete; \
  CLASS &operator= (CLASS &&) = delete;

#define LINN_DISABLE_COPY_MOVE(CLASS) \
  LINN_DISABLE_COPY(CLASS)            \
  LINN_DISABLE_MOVE(CLASS)

#define LINN_DECLARE_PRIVATE(CLASS)                                  \
  friend class CLASS##Private;                                       \
  [[nodiscard]] inline CLASS##Private *d_func() noexcept             \
  {                                                                  \
    return d_ptr.get();                                              \
  }                                                                  \
  [[nodiscard]] inline const CLASS##Private *d_func() const noexcept \
  {                                                                  \
    return d_ptr.get();                                              \
  }

#define LINN_DECLARE_PUBLIC(CLASS)                          \
  friend class CLASS;                                       \
  [[nodiscard]] inline CLASS *q_func() noexcept             \
  {                                                         \
    return q_ptr;                                           \
  }                                                         \
  [[nodiscard]] inline const CLASS *q_func() const noexcept \
  {                                                         \
    return q_ptr;                                           \
  }

#define LINN_D(CLASS) auto *const d = d_func()
#define LINN_Q(CLASS) auto *const q = q_func()

#define LINN_CONST_D(CLASS) auto const *const d = d_func()
#define LINN_CONST_Q(CLASS) auto const *const q = q_func()

#define LINN_Q_CREATE(CLASS)             \
                                         \
protected:                               \
  explicit CLASS(CLASS##Private &dd);    \
  std::unique_ptr<CLASS##Private> d_ptr; \
                                         \
private:                                 \
  LINN_DISABLE_COPY(CLASS)               \
  LINN_DECLARE_PRIVATE(CLASS)

#define LINN_D_CREATE(CLASS) \
                             \
protected:                   \
  CLASS *q_ptr;              \
                             \
private:                     \
  LINN_DECLARE_PUBLIC(CLASS)

#if defined(QT_DEBUG) || defined(QT_NO_DEBUG) || defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)

#  define Q_Q_CREATE(CLASS) LINN_Q_CREATE(CLASS)
#  define Q_D_CREATE(CLASS) LINN_D_CREATE(CLASS)

#  define Q_CONST_D(CLASS) LINN_CONST_D(CLASS)
#  define Q_CONST_Q(CLASS) LINN_CONST_Q(CLASS)

#endif

#endif // !LINN_PIMPL_H_
