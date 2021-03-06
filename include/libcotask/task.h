﻿/*
 * task.h
 *
 *  Created on: 2014年4月1日
 *      Author: owent
 *
 *  Released under the MIT license
 */

#ifndef COTASK_TASK_H
#define COTASK_TASK_H

#pragma once

#include <algorithm>
#include <stdint.h>

#include <libcopp/stack/stack_traits.h>
#include <libcopp/utils/errno.h>
#include <libcotask/task_macros.h>
#include <libcotask/this_task.h>


namespace cotask {

    template <typename TCO_MACRO = macro_coroutine, typename TTASK_MACRO = macro_task>
    class task : public impl::task_impl {
    public:
        typedef task<TCO_MACRO, TTASK_MACRO> self_t;
        typedef std::intrusive_ptr<self_t>   ptr_t;
        typedef TCO_MACRO                    macro_coroutine_t;
        typedef TTASK_MACRO                  macro_task_t;

        typedef typename macro_coroutine_t::coroutine_t       coroutine_t;
        typedef typename macro_coroutine_t::stack_allocator_t stack_allocator_t;

        typedef typename macro_task_t::id_t           id_t;
        typedef typename macro_task_t::id_allocator_t id_allocator_t;


        struct task_group {
            std::list<std::pair<ptr_t, void *> > member_list_;
        };

    private:
        typedef impl::task_impl::action_ptr_t action_ptr_t;

    public:
        /**
         * @brief constuctor
         * @note should not be called directly
         */
        task() : action_destroy_fn_(UTIL_CONFIG_NULLPTR) {
            id_allocator_t id_alloc_;
            id_ = id_alloc_.allocate();
            ref_count_.store(0);
        }


/**
 * @brief create task with functor
 * @param action
 * @param stack_size stack size
 * @param private_buffer_size buffer size to store private data
 * @return task smart pointer
 */
#if defined(UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES) && UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES
        template <typename TAct, typename Ty>
        static ptr_t create_with_delegate(Ty &&callable, typename coroutine_t::allocator_type &alloc, size_t stack_size = 0,
                                          size_t private_buffer_size = 0) {
#else
        template <typename TAct, typename Ty>
        static ptr_t create_with_delegate(const Ty &callable, typename coroutine_t::allocator_type &alloc, size_t stack_size = 0,
                                          size_t private_buffer_size = 0) {
#endif
            typedef TAct a_t;

            if (0 == stack_size) {
                stack_size = copp::stack_traits::default_size();
            }

            size_t action_size = coroutine_t::align_address_size(sizeof(a_t));
            size_t task_size   = coroutine_t::align_address_size(sizeof(self_t));

            if (stack_size <= sizeof(impl::task_impl *) + private_buffer_size + action_size + task_size) {
                return ptr_t();
            }

            typename coroutine_t::ptr_t coroutine = coroutine_t::create(
                (a_t *)(UTIL_CONFIG_NULLPTR), alloc, stack_size, sizeof(impl::task_impl *) + private_buffer_size, action_size + task_size);
            if (!coroutine) {
                return ptr_t();
            }

            void *action_addr = sub_buffer_offset(coroutine.get(), action_size);
            void *task_addr   = sub_buffer_offset(action_addr, task_size);

            // placement new task
            ptr_t ret(new (task_addr) self_t());
            if (!ret) {
                return ret;
            }

            *(reinterpret_cast<impl::task_impl **>(coroutine->get_private_buffer())) = ret.get();
            ret->coroutine_obj_                                                      = coroutine;
            ret->coroutine_obj_->set_flags(impl::task_impl::ext_coroutine_flag_t::EN_ECFT_COTASK);

            // placement new action
            a_t *action = new (action_addr) a_t(COPP_MACRO_STD_FORWARD(Ty, callable));
            if (UTIL_CONFIG_NULLPTR == action) {
                return ret;
            }

            typedef int (a_t::*a_t_fn_t)(void *);
            a_t_fn_t a_t_fn = &a_t::operator();

            // redirect runner
            coroutine->set_runner(
#if defined(UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES) && UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES
                std::move(std::bind(a_t_fn, action, std::placeholders::_1))
#else
                std::bind(a_t_fn, action, std::placeholders::_1)
#endif
            );

            ret->action_destroy_fn_ = get_placement_destroy(action);
            ret->_set_action(action);

            return ret;
        }


/**
 * @brief create task with functor
 * @param action
 * @param stack_size stack size
 * @param private_buffer_size buffer size to store private data
 * @return task smart pointer
 */
#if defined(UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES) && UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES
        template <typename Ty>
        static inline ptr_t create(Ty &&functor, size_t stack_size = 0, size_t private_buffer_size = 0) {
            typename coroutine_t::allocator_type alloc;
            return create(COPP_MACRO_STD_FORWARD(Ty, functor), alloc, stack_size, private_buffer_size);
        }

        template <typename Ty>
        static inline ptr_t create(Ty &&functor, typename coroutine_t::allocator_type &alloc, size_t stack_size = 0,
                                   size_t private_buffer_size = 0) {
            typedef typename std::conditional<std::is_base_of<impl::task_action_impl, Ty>::value, Ty, task_action_functor<Ty> >::type a_t;
            return create_with_delegate<a_t>(COPP_MACRO_STD_FORWARD(Ty, functor), alloc, stack_size, private_buffer_size);
        }
#else
        template <typename Ty>
        static inline ptr_t create(const Ty &functor, size_t stack_size = 0, size_t private_buffer_size = 0) {
            typename coroutine_t::allocator_type alloc;
            return create(functor, alloc, stack_size, private_buffer_size);
        }

        template <typename Ty>
        static ptr_t create(const Ty &functor, typename coroutine_t::allocator_type &alloc, size_t stack_size = 0,
                            size_t private_buffer_size = 0) {
            typedef typename std::conditional<std::is_base_of<impl::task_action_impl, Ty>::value, Ty, task_action_functor<Ty> >::type a_t;
            return create_with_delegate<a_t>(COPP_MACRO_STD_FORWARD(Ty, functor), alloc, stack_size, private_buffer_size);
        }
#endif

        /**
         * @brief create task with function
         * @param action
         * @param stack_size stack size
         * @return task smart pointer
         */
        template <typename Ty>
        static inline ptr_t create(Ty (*func)(void *), typename coroutine_t::allocator_type &alloc, size_t stack_size = 0,
                                   size_t private_buffer_size = 0) {
            typedef task_action_function<Ty> a_t;

            return create_with_delegate<a_t>(func, alloc, stack_size, private_buffer_size);
        }

        template <typename Ty>
        inline static ptr_t create(Ty (*func)(void *), size_t stack_size = 0, size_t private_buffer_size = 0) {
            typename coroutine_t::allocator_type alloc;
            return create(func, alloc, stack_size, private_buffer_size);
        }

        /**
         * @brief create task with function
         * @param action
         * @param stack_size stack size
         * @return task smart pointer
         */
        template <typename Ty, typename TInst>
        static ptr_t create(Ty(TInst::*func), TInst *instance, typename coroutine_t::allocator_type &alloc, size_t stack_size = 0,
                            size_t private_buffer_size = 0) {
            typedef task_action_mem_function<Ty, TInst> a_t;

            return create<a_t>(a_t(func, instance), alloc, stack_size, private_buffer_size);
        }

        template <typename Ty, typename TInst>
        inline static ptr_t create(Ty(TInst::*func), TInst *instance, size_t stack_size = 0, size_t private_buffer_size = 0) {
            typename coroutine_t::allocator_type alloc;
            return create(func, instance, alloc, stack_size, private_buffer_size);
        }

#if defined(UTIL_CONFIG_COMPILER_CXX_VARIADIC_TEMPLATES) && UTIL_CONFIG_COMPILER_CXX_VARIADIC_TEMPLATES
        /**
         * @brief create task with functor type and parameters
         * @param stack_size stack size
         * @param args all parameters passed to construtor of type Ty
         * @return task smart pointer
         */
        template <typename Ty, typename... TParams>
        static ptr_t create_with(typename coroutine_t::allocator_type &alloc, size_t stack_size, size_t private_buffer_size,
                                 TParams &&... args) {
            typedef Ty a_t;

            return create(COPP_MACRO_STD_MOVE(a_t(COPP_MACRO_STD_FORWARD(TParams, args)...)), alloc, stack_size, private_buffer_size);
        }
#endif


        /**
         * @brief await another cotask to finish
         * @note please not to make tasks refer to each other. [it will lead to memory leak]
         * @note [don't do that] ptr_t a = ..., b = ...; a.await(b); b.await(a);
         * @param wait_task which stack to wait for
         * @return 0 or error code
         */
        inline int await(ptr_t wait_task) {
            if (!wait_task) {
                return copp::COPP_EC_ARGS_ERROR;
            }

            if (this == wait_task.get()) {
                return copp::COPP_EC_TASK_CAN_NOT_WAIT_SELF;
            }

            // if target is exiting or completed, just return
            if (wait_task->is_exiting() || wait_task->is_completed()) {
                return copp::COPP_EC_TASK_IS_EXITING;
            }

            if (is_exiting()) {
                return copp::COPP_EC_TASK_IS_EXITING;
            }

            if (this_task() != this) {
                return copp::COPP_EC_TASK_NOT_IN_ACTION;
            }

            // add to next list failed
            if (wait_task->next(ptr_t(this)).get() != this) {
                return copp::COPP_EC_TASK_ADD_NEXT_FAILED;
            }

            int ret = 0;
            while (!(wait_task->is_exiting() || wait_task->is_completed())) {
                if (is_exiting()) {
                    return copp::COPP_EC_TASK_IS_EXITING;
                }

                ret = yield();
            }

            return ret;
        }

        template <typename TTask>
        inline int await(TTask *wait_task) {
            return await(ptr_t(wait_task));
        }

        /**
         * @brief add next task to run when task finished
         * @note please not to make tasks refer to each other. [it will lead to memory leak]
         * @note [don't do that] ptr_t a = ..., b = ...; a.next(b); b.next(a);
         * @param next_task next stack
         * @param priv_data priv_data passed to resume or start next stack
         * @return next_task if success , or self if failed
         */
        inline ptr_t next(ptr_t next_task, void *priv_data = UTIL_CONFIG_NULLPTR) {
            // can not refers to self
            if (this == next_task.get()) {
                return ptr_t(this);
            }

            // can not add next task when finished
            if (is_exiting()) {
                return ptr_t(this);
            }

#if !defined(PROJECT_DISABLE_MT) || !(PROJECT_DISABLE_MT)
            util::lock::lock_holder<util::lock::spin_lock> lock_guard(next_list_lock_);
#endif

            next_list_.member_list_.push_back(std::make_pair(next_task, priv_data));
            return next_task;
        }

/**
 * @brief create next task with functor
 * @see next
 * @param functor
 * @param priv_data priv_data passed to start functor
 * @param stack_size stack size
 * @return the created task if success , or self if failed
 */
#if defined(UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES) && UTIL_CONFIG_COMPILER_CXX_RVALUE_REFERENCES
        template <typename Ty>
        inline ptr_t next(Ty &&functor, void *priv_data = UTIL_CONFIG_NULLPTR, size_t stack_size = 0, size_t private_buffer_size = 0) {
            return next(create(std::forward<Ty>(functor), stack_size, private_buffer_size), priv_data);
        }

        template <typename Ty>
        inline ptr_t next(Ty &&functor, typename coroutine_t::allocator_type &alloc, void *priv_data = UTIL_CONFIG_NULLPTR,
                          size_t stack_size = 0, size_t private_buffer_size = 0) {
            return next(create(std::forward<Ty>(functor), alloc, stack_size, private_buffer_size), priv_data);
        }
#else
        template <typename Ty>
        inline ptr_t next(const Ty &functor, void *priv_data = UTIL_CONFIG_NULLPTR, size_t stack_size = 0, size_t private_buffer_size = 0) {
            return next(create(functor, stack_size, private_buffer_size), priv_data);
        }

        template <typename Ty>
        inline ptr_t next(const Ty &functor, typename coroutine_t::allocator_type &alloc, void *priv_data = UTIL_CONFIG_NULLPTR,
                          size_t stack_size = 0, size_t private_buffer_size = 0) {
            return next(create(std::forward<Ty>(functor), alloc, stack_size, private_buffer_size), priv_data);
        }
#endif

        /**
         * @brief create next task with function
         * @see next
         * @param func function
         * @param priv_data priv_data passed to start function
         * @param stack_size stack size
         * @return the created task if success , or self if failed
         */
        template <typename Ty>
        inline ptr_t next(Ty (*func)(void *), void *priv_data = UTIL_CONFIG_NULLPTR, size_t stack_size = 0,
                          size_t private_buffer_size = 0) {
            return next(create(func, stack_size, private_buffer_size), priv_data);
        }

        template <typename Ty>
        inline ptr_t next(Ty (*func)(void *), typename coroutine_t::allocator_type &alloc, void *priv_data = UTIL_CONFIG_NULLPTR,
                          size_t stack_size = 0, size_t private_buffer_size = 0) {
            return next(create(func, alloc, stack_size, private_buffer_size), priv_data);
        }

        /**
         * @brief create next task with function
         * @see next
         * @param func member function
         * @param instance instance
         * @param priv_data priv_data passed to start (instance->*func)(priv_data)
         * @param stack_size stack size
         * @return the created task if success , or self if failed
         */
        template <typename Ty, typename TInst>
        inline ptr_t next(Ty(TInst::*func), TInst *instance, void *priv_data = UTIL_CONFIG_NULLPTR, size_t stack_size = 0,
                          size_t private_buffer_size = 0) {
            return next(create(func, instance, stack_size, private_buffer_size), priv_data);
        }

        template <typename Ty, typename TInst>
        inline ptr_t next(Ty(TInst::*func), TInst *instance, typename coroutine_t::allocator_type &alloc,
                          void *priv_data = UTIL_CONFIG_NULLPTR, size_t stack_size = 0, size_t private_buffer_size = 0) {
            return next(create(func, instance, alloc, stack_size, private_buffer_size), priv_data);
        }

        /**
         * get current running task and convert to task object
         * @return task smart pointer
         */
        static self_t *this_task() { return dynamic_cast<self_t *>(impl::task_impl::this_task()); }

    public:
        virtual ~task() {
            EN_TASK_STATUS status = get_status();
            // inited but not finished will trigger timeout or finish other actor
            if (status < EN_TS_DONE && status > EN_TS_CREATED) {
                kill(EN_TS_TIMEOUT);
            }

            // free resource
            id_allocator_t id_alloc_;
            id_alloc_.deallocate(id_);
        }

        inline typename coroutine_t::ptr_t &      get_coroutine_context() UTIL_CONFIG_NOEXCEPT { return coroutine_obj_; }
        inline const typename coroutine_t::ptr_t &get_coroutine_context() const UTIL_CONFIG_NOEXCEPT { return coroutine_obj_; }

        inline id_t get_id() const UTIL_CONFIG_NOEXCEPT { return id_; }

    public:
        virtual int get_ret_code() const UTIL_CONFIG_OVERRIDE {
            if (!coroutine_obj_) {
                return 0;
            }

            return coroutine_obj_->get_ret_code();
        }

        virtual int start(void *priv_data, EN_TASK_STATUS expected_status = EN_TS_CREATED) UTIL_CONFIG_OVERRIDE {
            if (!coroutine_obj_) {
                return copp::COPP_EC_NOT_INITED;
            }

            EN_TASK_STATUS from_status = expected_status;

            do {
                if (unlikely(from_status >= EN_TS_DONE)) {
                    return copp::COPP_EC_ALREADY_FINISHED;
                }

                if (unlikely(from_status == EN_TS_RUNNING)) {
                    return copp::COPP_EC_IS_RUNNING;
                }

                if (likely(_cas_status(from_status, EN_TS_RUNNING))) { // Atomic.CAS here
                    break;
                }
            } while (true);

            // use this smart ptr to avoid destroy of this
            // ptr_t protect_from_destroy(this);

            int ret = coroutine_obj_->start(priv_data);

            from_status = EN_TS_RUNNING;
            if (is_completed()) { // Atomic.CAS here
                while (from_status < EN_TS_DONE) {
                    if (likely(_cas_status(from_status, EN_TS_DONE))) { // Atomic.CAS here
                        break;
                    }
                }

                finish_priv_data_ = priv_data;
                _notify_finished(priv_data);
                return ret;
            }

            while (true) {
                if (from_status >= EN_TS_DONE) { // canceled or killed
                    _notify_finished(finish_priv_data_);
                    break;
                }

                if (likely(_cas_status(from_status, EN_TS_WAITING))) { // Atomic.CAS here
                    break;
                    // waiting
                }
            }

            return ret;
        }

        virtual int resume(void *priv_data, EN_TASK_STATUS expected_status = EN_TS_WAITING) UTIL_CONFIG_OVERRIDE {
            return start(priv_data, expected_status);
        }

        virtual int yield(void **priv_data) UTIL_CONFIG_OVERRIDE {
            if (!coroutine_obj_) {
                return copp::COPP_EC_NOT_INITED;
            }

            return coroutine_obj_->yield(priv_data);
        }

        virtual int cancel(void *priv_data) UTIL_CONFIG_OVERRIDE {
            EN_TASK_STATUS from_status = get_status();

            do {
                if (EN_TS_RUNNING == from_status) {
                    return copp::COPP_EC_IS_RUNNING;
                }

                if (likely(_cas_status(from_status, EN_TS_CANCELED))) {
                    break;
                }
            } while (true);

            _notify_finished(priv_data);
            return copp::COPP_EC_SUCCESS;
        }

        virtual int kill(enum EN_TASK_STATUS status, void *priv_data) UTIL_CONFIG_OVERRIDE {
            EN_TASK_STATUS from_status = get_status();

            do {
                if (likely(_cas_status(from_status, status))) {
                    break;
                }
            } while (true);

            if (EN_TS_RUNNING != from_status) {
                _notify_finished(priv_data);
            } else {
                finish_priv_data_ = priv_data;
            }

            return copp::COPP_EC_SUCCESS;
        }

        using impl::task_impl::cancel;
        using impl::task_impl::kill;
        using impl::task_impl::resume;
        using impl::task_impl::start;
        using impl::task_impl::yield;

    public:
        virtual bool is_completed() const UTIL_CONFIG_NOEXCEPT UTIL_CONFIG_OVERRIDE {
            if (!coroutine_obj_) {
                return false;
            }

            return coroutine_obj_->is_finished();
        }

        static inline void *add_buffer_offset(void *in, size_t off) {
            return reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(in) + off);
        }

        static inline void *sub_buffer_offset(void *in, size_t off) {
            return reinterpret_cast<void *>(reinterpret_cast<unsigned char *>(in) - off);
        }

        void *get_private_buffer() {
            if (!coroutine_obj_) {
                return UTIL_CONFIG_NULLPTR;
            }

            return add_buffer_offset(coroutine_obj_->get_private_buffer(), sizeof(impl::task_impl *));
        }

        size_t get_private_buffer_size() {
            if (!coroutine_obj_) {
                return 0;
            }

            return coroutine_obj_->get_private_buffer_size() - sizeof(impl::task_impl *);
        }


        inline size_t use_count() const { return ref_count_.load(); }

    private:
        task(const task &) UTIL_CONFIG_DELETED_FUNCTION;

        void active_next_tasks() {
            std::list<std::pair<ptr_t, void *> > next_list;

            // first, lock and swap container
            {
#if !defined(PROJECT_DISABLE_MT) || !(PROJECT_DISABLE_MT)
                util::lock::lock_holder<util::lock::spin_lock> lock_guard(next_list_lock_);
#endif
                next_list.swap(next_list_.member_list_);
            }

            // then, do all the pending tasks
            for (typename std::list<std::pair<ptr_t, void *> >::iterator iter = next_list.begin(); iter != next_list.end(); ++iter) {
                if (!iter->first || EN_TS_INVALID == iter->first->get_status()) {
                    continue;
                }

                if (iter->first->get_status() < EN_TS_RUNNING) {
                    iter->first->start(iter->second);
                } else {
                    iter->first->resume(iter->second);
                }
            }
        }

        int _notify_finished(void *priv_data) {
            // first, make sure coroutine finished.
            if (coroutine_obj_ && false == coroutine_obj_->is_finished()) {
                // make sure this task will not be destroyed when running
                while (false == coroutine_obj_->is_finished()) {
                    coroutine_obj_->resume(priv_data);
                }
            }

            int ret = impl::task_impl::_notify_finished(priv_data);

            // next tasks
            active_next_tasks();
            return ret;
        }


        friend void intrusive_ptr_add_ref(self_t *p) {
            if (p == UTIL_CONFIG_NULLPTR) {
                return;
            }

            ++p->ref_count_;
        }

        friend void intrusive_ptr_release(self_t *p) {
            if (p == UTIL_CONFIG_NULLPTR) {
                return;
            }

            size_t left = --p->ref_count_;
            if (0 == left) {
                // save coroutine context first, make sure it's still available after destroy task
                typename coroutine_t::ptr_t coro = p->coroutine_obj_;

                // then, find and destroy action
                void *action_ptr = reinterpret_cast<void *>(p->_get_action());
                if (UTIL_CONFIG_NULLPTR != p->action_destroy_fn_ && UTIL_CONFIG_NULLPTR != action_ptr) {
                    (*p->action_destroy_fn_)(action_ptr);
                }

                // then, destruct task
                p->~task();

                // at last, destroy the coroutine and maybe recycle the stack space
                coro.reset();
            }
        }

    private:
        id_t                        id_;
        typename coroutine_t::ptr_t coroutine_obj_;
        task_group                  next_list_;

        // ============== action information ==============
        void (*action_destroy_fn_)(void *);

#if !defined(PROJECT_DISABLE_MT) || !(PROJECT_DISABLE_MT)
        util::lock::atomic_int_type<size_t> ref_count_; /** ref_count **/
        util::lock::spin_lock               next_list_lock_;
#else
        util::lock::atomic_int_type<util::lock::unsafe_int_type<size_t> > ref_count_; /** ref_count **/
#endif
    };
} // namespace cotask


#endif /* _COTASK_TASK_H_ */
