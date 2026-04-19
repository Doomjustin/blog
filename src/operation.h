#ifndef BLOG_OPERATION_H
#define BLOG_OPERATION_H

struct Operation {
    virtual ~Operation() = default;

    virtual void complete(int res, unsigned flags) = 0;
};

#endif // BLOG_OPERATION_H