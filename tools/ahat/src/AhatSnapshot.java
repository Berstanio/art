/*
 * Copyright (C) 2015 The Android Open Source Project
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

package com.android.ahat;

import com.android.tools.perflib.heap.ClassObj;
import com.android.tools.perflib.heap.Heap;
import com.android.tools.perflib.heap.HprofParser;
import com.android.tools.perflib.heap.Instance;
import com.android.tools.perflib.heap.RootObj;
import com.android.tools.perflib.heap.Snapshot;
import com.android.tools.perflib.heap.StackFrame;
import com.android.tools.perflib.heap.StackTrace;
import com.android.tools.perflib.heap.io.HprofBuffer;
import com.android.tools.perflib.heap.io.MemoryMappedFileBuffer;
import com.google.common.collect.Iterables;
import com.google.common.collect.Lists;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * A wrapper over the perflib snapshot that provides the behavior we use in
 * ahat.
 */
class AhatSnapshot {
  private Snapshot mSnapshot;
  private List<Heap> mHeaps;

  // Map from Instance to the list of Instances it immediately dominates.
  private Map<Instance, List<Instance>> mDominated;

  private Site mRootSite;
  private Map<Heap, Long> mHeapSizes;

  /**
   * Create an AhatSnapshot from an hprof file.
   */
  public static AhatSnapshot fromHprof(File hprof) throws IOException {
    HprofBuffer buffer = new MemoryMappedFileBuffer(hprof);
    Snapshot snapshot = (new HprofParser(buffer)).parse();
    snapshot.computeDominators();
    return new AhatSnapshot(snapshot);
  }

  /**
   * Construct an AhatSnapshot for the given perflib snapshot.
   * Ther user is responsible for calling snapshot.computeDominators before
   * calling this AhatSnapshot constructor.
   */
  private AhatSnapshot(Snapshot snapshot) {
    mSnapshot = snapshot;
    mHeaps = new ArrayList<Heap>(mSnapshot.getHeaps());
    mDominated = new HashMap<Instance, List<Instance>>();
    mRootSite = new Site("ROOT");
    mHeapSizes = new HashMap<Heap, Long>();

    ClassObj javaLangClass = mSnapshot.findClass("java.lang.Class");
    for (Heap heap : mHeaps) {
      long total = 0;
      for (Instance inst : Iterables.concat(heap.getClasses(), heap.getInstances())) {
        Instance dominator = inst.getImmediateDominator();
        if (dominator != null) {
          total += inst.getSize();

          // Properly label the class of a class object.
          if (inst instanceof ClassObj && javaLangClass != null && inst.getClassObj() == null) {
              inst.setClassId(javaLangClass.getId());
          }

          // Update dominated instances.
          List<Instance> instances = mDominated.get(dominator);
          if (instances == null) {
            instances = new ArrayList<Instance>();
            mDominated.put(dominator, instances);
          }
          instances.add(inst);

          // Update sites.
          List<StackFrame> path = Collections.emptyList();
          StackTrace stack = getStack(inst);
          int stackId = getStackTraceSerialNumber(stack);
          if (stack != null) {
            StackFrame[] frames = getStackFrames(stack);
            if (frames != null && frames.length > 0) {
              path = Lists.reverse(Arrays.asList(frames));
            }
          }
          mRootSite.add(stackId, 0, path.iterator(), inst);
        }
      }
      mHeapSizes.put(heap, total);
    }
  }

  // Note: This method is exposed for testing purposes.
  public ClassObj findClass(String name) {
    return mSnapshot.findClass(name);
  }

  public Instance findInstance(long id) {
    return mSnapshot.findInstance(id);
  }

  public int getHeapIndex(Heap heap) {
    return mSnapshot.getHeapIndex(heap);
  }

  public Heap getHeap(String name) {
    return mSnapshot.getHeap(name);
  }

  public Collection<RootObj> getGCRoots() {
    return mSnapshot.getGCRoots();
  }

  public List<Heap> getHeaps() {
    return mHeaps;
  }

  public Site getRootSite() {
    return mRootSite;
  }

  /**
   * Look up the site at which the given object was allocated.
   */
  public Site getSiteForInstance(Instance inst) {
    Site site = mRootSite;
    StackTrace stack = getStack(inst);
    if (stack != null) {
      StackFrame[] frames = getStackFrames(stack);
      if (frames != null) {
        List<StackFrame> path = Lists.reverse(Arrays.asList(frames));
        site = mRootSite.getChild(path.iterator());
      }
    }
    return site;
  }

  /**
   * Return a list of those objects immediately dominated by the given
   * instance.
   */
  public List<Instance> getDominated(Instance inst) {
    return mDominated.get(inst);
  }

  /**
   * Return the total size of reachable objects allocated on the given heap.
   */
  public long getHeapSize(Heap heap) {
    return mHeapSizes.get(heap);
  }

  /**
   * Return the class name for the given class object.
   * classObj may be null, in which case "(class unknown)" is returned.
   */
  public static String getClassName(ClassObj classObj) {
    if (classObj == null) {
      return "(class unknown)";
    }
    return classObj.getClassName();
  }

  // Return the stack where the given instance was allocated.
  private static StackTrace getStack(Instance inst) {
    // TODO: return inst.getStack() once perflib is fixed.
    return null;
  }

  // Return the list of stack frames for a stack trace.
  private static StackFrame[] getStackFrames(StackTrace stack) {
    // TODO: Use stack.getFrames() once perflib is fixed.
    return null;
  }

  // Return the serial number of the given stack trace.
  private static int getStackTraceSerialNumber(StackTrace stack) {
    // TODO: Use stack.getSerialNumber() once perflib is fixed.
    return 0;
  }

  // Get the site associated with the given stack id and depth.
  // Returns the root site if no such site found.
  // depth of -1 means the full stack.
  public Site getSite(int stackId, int depth) {
    Site site = mRootSite;
    StackTrace stack = mSnapshot.getStackTrace(stackId);
    if (stack != null) {
      StackFrame[] frames = getStackFrames(stack);
      if (frames != null) {
        List<StackFrame> path = Lists.reverse(Arrays.asList(frames));
        if (depth >= 0) {
          path = path.subList(0, depth);
        }
        site = mRootSite.getChild(path.iterator());
      }
    }
    return site;
  }
}
