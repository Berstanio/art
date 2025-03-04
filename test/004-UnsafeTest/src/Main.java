/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import java.lang.reflect.Field;
import sun.misc.Unsafe;

public class Main {
  private static void check(int actual, int expected, String msg) {
    if (actual != expected) {
      System.out.println(msg + " : " + actual + " != " + expected);
      System.exit(1);
    }
  }

  private static void check(long actual, long expected, String msg) {
    if (actual != expected) {
      System.out.println(msg + " : " + actual + " != " + expected);
      System.exit(1);
    }
  }

  private static void check(Object actual, Object expected, String msg) {
    if (actual != expected) {
      System.out.println(msg + " : " + actual + " != " + expected);
      System.exit(1);
    }
  }

  private static Unsafe getUnsafe() throws Exception {
    Class<?> unsafeClass = Class.forName("sun.misc.Unsafe");
    Field f = unsafeClass.getDeclaredField("theUnsafe");
    f.setAccessible(true);
    return (Unsafe) f.get(null);
  }

  public static void main(String[] args) throws Exception {
    System.loadLibrary(args[0]);
    Unsafe unsafe = getUnsafe();
    check(unsafe.arrayBaseOffset(boolean[].class), vmArrayBaseOffset(boolean[].class),
        "Unsafe.arrayBaseOffset(boolean[])");
    check(unsafe.arrayBaseOffset(byte[].class), vmArrayBaseOffset(byte[].class),
        "Unsafe.arrayBaseOffset(byte[])");
    check(unsafe.arrayBaseOffset(char[].class), vmArrayBaseOffset(char[].class),
        "Unsafe.arrayBaseOffset(char[])");
    check(unsafe.arrayBaseOffset(double[].class), vmArrayBaseOffset(double[].class),
        "Unsafe.arrayBaseOffset(double[])");
    check(unsafe.arrayBaseOffset(float[].class), vmArrayBaseOffset(float[].class),
        "Unsafe.arrayBaseOffset(float[])");
    check(unsafe.arrayBaseOffset(int[].class), vmArrayBaseOffset(int[].class),
        "Unsafe.arrayBaseOffset(int[])");
    check(unsafe.arrayBaseOffset(long[].class), vmArrayBaseOffset(long[].class),
        "Unsafe.arrayBaseOffset(long[])");
    check(unsafe.arrayBaseOffset(Object[].class), vmArrayBaseOffset(Object[].class),
        "Unsafe.arrayBaseOffset(Object[])");

    check(unsafe.arrayIndexScale(boolean[].class), vmArrayIndexScale(boolean[].class),
        "Unsafe.arrayIndexScale(boolean[])");
    check(unsafe.arrayIndexScale(byte[].class), vmArrayIndexScale(byte[].class),
        "Unsafe.arrayIndexScale(byte[])");
    check(unsafe.arrayIndexScale(char[].class), vmArrayIndexScale(char[].class),
        "Unsafe.arrayIndexScale(char[])");
    check(unsafe.arrayIndexScale(double[].class), vmArrayIndexScale(double[].class),
        "Unsafe.arrayIndexScale(double[])");
    check(unsafe.arrayIndexScale(float[].class), vmArrayIndexScale(float[].class),
        "Unsafe.arrayIndexScale(float[])");
    check(unsafe.arrayIndexScale(int[].class), vmArrayIndexScale(int[].class),
        "Unsafe.arrayIndexScale(int[])");
    check(unsafe.arrayIndexScale(long[].class), vmArrayIndexScale(long[].class),
        "Unsafe.arrayIndexScale(long[])");
    check(unsafe.arrayIndexScale(Object[].class), vmArrayIndexScale(Object[].class),
        "Unsafe.arrayIndexScale(Object[])");

    TestClass t = new TestClass();

    int intValue = 12345678;
    Field intField = TestClass.class.getDeclaredField("intVar");
    long intOffset = unsafe.objectFieldOffset(intField);
    check(unsafe.getInt(t, intOffset), 0, "Unsafe.getInt(Object, long) - initial");
    unsafe.putInt(t, intOffset, intValue);
    check(t.intVar, intValue, "Unsafe.putInt(Object, long, int)");
    check(unsafe.getInt(t, intOffset), intValue, "Unsafe.getInt(Object, long)");

    long longValue = 1234567887654321L;
    Field longField = TestClass.class.getDeclaredField("longVar");
    long longOffset = unsafe.objectFieldOffset(longField);
    check(unsafe.getLong(t, longOffset), 0, "Unsafe.getLong(Object, long) - initial");
    unsafe.putLong(t, longOffset, longValue);
    check(t.longVar, longValue, "Unsafe.putLong(Object, long, long)");
    check(unsafe.getLong(t, longOffset), longValue, "Unsafe.getLong(Object, long)");

    Object objectValue = new Object();
    Field objectField = TestClass.class.getDeclaredField("objectVar");
    long objectOffset = unsafe.objectFieldOffset(objectField);
    check(unsafe.getObject(t, objectOffset), null, "Unsafe.getObject(Object, long) - initial");
    unsafe.putObject(t, objectOffset, objectValue);
    check(t.objectVar, objectValue, "Unsafe.putObject(Object, long, Object)");
    check(unsafe.getObject(t, objectOffset), objectValue, "Unsafe.getObject(Object, long)");

    if (unsafe.compareAndSwapInt(t, intOffset, 0, 1)) {
        System.out.println("Unexpectedly succeeding compareAndSwap...");
    }
    if (!unsafe.compareAndSwapInt(t, intOffset, intValue, 0)) {
        System.out.println("Unexpectedly not succeeding compareAndSwap...");
    }
    if (!unsafe.compareAndSwapInt(t, intOffset, 0, 1)) {
        System.out.println("Unexpectedly not succeeding compareAndSwap...");
    }

    if (unsafe.compareAndSwapLong(t, longOffset, 0, 1)) {
        System.out.println("Unexpectedly succeeding compareAndSwapLong...");
    }
    if (!unsafe.compareAndSwapLong(t, longOffset, longValue, 0)) {
        System.out.println("Unexpectedly not succeeding compareAndSwapLong...");
    }
    if (!unsafe.compareAndSwapLong(t, longOffset, 0, 1)) {
        System.out.println("Unexpectedly not succeeding compareAndSwapLong...");
    }

    if (unsafe.compareAndSwapObject(t, objectOffset, null, new Object())) {
        System.out.println("Unexpectedly succeeding compareAndSwapObject...");
    }
    if (!unsafe.compareAndSwapObject(t, objectOffset, objectValue, null)) {
        System.out.println("Unexpectedly not succeeding compareAndSwapObject...");
    }
    if (!unsafe.compareAndSwapObject(t, objectOffset, null, new Object())) {
        System.out.println("Unexpectedly not succeeding compareAndSwapObject...");
    }
  }

  private static class TestClass {
    public int intVar = 0;
    public long longVar = 0;
    public Object objectVar = null;
  }

  private static native int vmArrayBaseOffset(Class clazz);
  private static native int vmArrayIndexScale(Class clazz);
}
