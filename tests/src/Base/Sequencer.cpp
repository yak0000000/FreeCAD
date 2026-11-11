// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Base/Sequencer.h"

using ::testing::_;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::StrEq;

namespace
{

// ---------------------------------------------------------------------------
// Test doubles
//
// SequencerBase registers itself in its constructor, so the most recently created
// object becomes the active sequencer returned by SequencerBase::Instance().
// Creating one of the classes below in a test therefore replaces the real progress
// indicator (console output or the Gui progress bar) for the code under test.
// ---------------------------------------------------------------------------

// Stub: the visual hooks do nothing, so the test checks only the bookkeeping
// of SequencerBase (steps, percent, cancel state) and nothing is printed.
class StubSequencer: public Base::SequencerBase
{
public:
    // start(), next(), ... are protected in SequencerBase, make them callable from tests
    using SequencerBase::next;
    using SequencerBase::numberOfSteps;
    using SequencerBase::progressInPercent;
    using SequencerBase::rejectCancel;
    using SequencerBase::start;
    using SequencerBase::stop;
    using SequencerBase::tryToCancel;

protected:
    void setText(const char* /*text*/) override
    {}
    void startStep() override
    {}
    void nextStep(bool /*canAbort*/) override
    {}
    void stopStep() override
    {}
    // resetData() is intentionally not overridden: the base implementation
    // clears the cancel flag and that is part of the tested behaviour
};

// Fake: a simplified but working progress indicator, which "draws" into memory
// instead of the screen, so the test can inspect what a user would have seen.
class FakeProgressBar: public Base::SequencerBase
{
public:
    using SequencerBase::next;
    using SequencerBase::start;
    using SequencerBase::stop;

    std::vector<std::string> texts;  // every text shown to the user
    std::vector<int> shownPercents;  // every percent value the bar was redrawn with
    int startCount = 0;
    int stopCount = 0;

protected:
    void setText(const char* text) override
    {
        texts.emplace_back(text);
    }
    void startStep() override
    {
        ++startCount;
    }
    void nextStep(bool /*canAbort*/) override
    {
        shownPercents.push_back(progressInPercent());
    }
    void stopStep() override
    {
        ++stopCount;
    }
};

// Mock: records the calls of the hooks, the test states up front which calls
// it expects (EXPECT_CALL) and gMock verifies them when the mock is destroyed.
class MockSequencer: public Base::SequencerBase
{
public:
    using SequencerBase::next;
    using SequencerBase::start;

    MOCK_METHOD(void, setText, (const char* text), (override));
    MOCK_METHOD(void, startStep, (), (override));
    MOCK_METHOD(void, nextStep, (bool canAbort), (override));
    MOCK_METHOD(void, stopStep, (), (override));
    MOCK_METHOD(void, setProgress, (size_t value), (override));
    MOCK_METHOD(void, resetData, (), (override));
};

}  // namespace

// ---------------------------------------------------------------------------
// Example 1 (stub): checks the return value of next()
// ---------------------------------------------------------------------------
TEST(Sequencer, nextReturnsFalseOnLastStep)
{
    // Arrange
    StubSequencer sequencer;
    sequencer.start("work", 3);

    // Act + Assert: next() reports whether there are steps left
    EXPECT_TRUE(sequencer.next());   // step 1 of 3
    EXPECT_TRUE(sequencer.next());   // step 2 of 3
    EXPECT_FALSE(sequencer.next());  // step 3 of 3, nothing left
}

// ---------------------------------------------------------------------------
// Example 2 (mock): checks which hooks SequencerLauncher calls and in which order
// ---------------------------------------------------------------------------
TEST(SequencerLauncher, startsAndStopsActiveSequencer)
{
    // Arrange: created last, so it is the active sequencer for the launcher
    NiceMock<MockSequencer> sequencer;

    // Expect: in gMock the expectations are written BEFORE the code is executed
    {
        InSequence order;  // the calls below must happen in exactly this order
        EXPECT_CALL(sequencer, setText(StrEq("loading")));
        EXPECT_CALL(sequencer, startStep());
        EXPECT_CALL(sequencer, stopStep());
    }

    // Act: the constructor starts the sequencer, the destructor stops it
    {
        Base::SequencerLauncher launcher("loading", 10);
    }

    // Assert: done by gMock automatically when 'sequencer' goes out of scope
}

// ---------------------------------------------------------------------------
// TODO: your tests. Use the double named in brackets.
// ---------------------------------------------------------------------------

// 3. (stub) start() sets numberOfSteps() and resets progressInPercent() to -1
TEST(Sequencer, startSetsNumberOfStepsAndResetsPercent)
{
    // Arrange
    StubSequencer sequencer;
    sequencer.start("x", 5);

    // Act + Assert
    EXPECT_EQ(sequencer.numberOfSteps(), 5u);
    EXPECT_EQ(sequencer.progressInPercent(), -1);
}

// 4. (fake) the bar is redrawn only when the percent grows, not on every step
TEST(Sequencer, nextRedrawsOnlyWhenPercentGrows)
{
    // Arrange
    FakeProgressBar bar;
    bar.start("x", 1000);

    // Act
    for (int i = 0; i < 1000; ++i) {
        bar.next();
    }

    // Assert: percent values 0, 1, ..., 100 were each shown exactly once
    ASSERT_EQ(bar.shownPercents.size(), 101u);  // ASSERT: front()/back() below need data
    EXPECT_EQ(bar.shownPercents.front(), 0);
    EXPECT_EQ(bar.shownPercents.back(), 100);
    EXPECT_EQ(bar.texts, std::vector<std::string> {"x"});
    EXPECT_EQ(bar.startCount, 1);
}

// 5. (stub) with an unknown number of steps the percent is computed as if there were 1000
TEST(Sequencer, unknownNumberOfStepsUsesThousandAsDivisor)
{
    // Arrange
    StubSequencer sequencer;
    sequencer.start("x", 0);

    // Act + Assert: there are never "steps left", but the progress still moves
    EXPECT_FALSE(sequencer.next());
    for (int i = 1; i < 500; ++i) {
        sequencer.next();
    }
    EXPECT_EQ(sequencer.progressInPercent(), 50);  // 500 steps of 1000
}

// 6. (mock) a locked sequencer does not update the progress indicator
TEST(Sequencer, lockedSequencerDoesNotCallStepHooks)
{
    // Arrange
    NiceMock<MockSequencer> sequencer;
    sequencer.setLocked(true);

    // Expect: start() sets the text even when locked, the step hooks are skipped
    EXPECT_CALL(sequencer, setText(StrEq("x")));
    EXPECT_CALL(sequencer, startStep()).Times(0);
    EXPECT_CALL(sequencer, nextStep(_)).Times(0);

    // Act
    sequencer.start("x", 10);
    for (int i = 0; i < 10; ++i) {
        sequencer.next();
    }
}

// 7. (stub) a cancel request is visible until the sequencer is stopped
TEST(Sequencer, stopClearsCancelRequest)
{
    // Arrange
    StubSequencer sequencer;
    sequencer.start("x", 3);

    // Act + Assert
    EXPECT_FALSE(sequencer.wasCanceled());
    sequencer.tryToCancel();
    EXPECT_TRUE(sequencer.wasCanceled());
    sequencer.stop();
    EXPECT_FALSE(sequencer.wasCanceled());
}

// 8. (stub) rejectCancel() withdraws a cancel request
TEST(Sequencer, rejectCancelClearsCancelRequest)
{
    // Arrange
    StubSequencer sequencer;
    sequencer.tryToCancel();

    // Act
    sequencer.rejectCancel();

    // Assert
    EXPECT_FALSE(sequencer.wasCanceled());
}

// 9. (fake) only the outermost launcher drives the progress, nested ones are ignored
TEST(SequencerLauncher, nestedLauncherDoesNotRestartSequencer)
{
    // Arrange
    FakeProgressBar bar;
    {
        Base::SequencerLauncher outer("outer", 10);
        {
            // Act
            Base::SequencerLauncher inner("inner", 5);
            EXPECT_TRUE(inner.next());
        }

        // Assert: the inner launcher neither started, moved nor stopped the bar
        EXPECT_EQ(bar.startCount, 1);
        EXPECT_EQ(bar.texts, std::vector<std::string> {"outer"});
        EXPECT_TRUE(bar.shownPercents.empty());
        EXPECT_EQ(bar.stopCount, 0);
    }

    // Assert: the outer launcher stopped the bar when it was destroyed
    EXPECT_EQ(bar.stopCount, 1);
}

// 10. (stub) Instance() returns the most recently created sequencer
TEST(Sequencer, instanceReturnsMostRecentlyCreatedSequencer)
{
    // Arrange
    StubSequencer first;
    {
        StubSequencer second;

        // Act + Assert
        EXPECT_EQ(&Base::SequencerBase::Instance(), &second);
    }

    // Assert: after 'second' is destroyed the previous one is active again
    EXPECT_EQ(&Base::SequencerBase::Instance(), &first);
}

// 11. (stub) isRunning() is true only while a launcher exists
TEST(SequencerLauncher, isRunningOnlyWhileLauncherExists)
{
    // Arrange
    StubSequencer sequencer;
    EXPECT_FALSE(sequencer.isRunning());

    // Act + Assert
    {
        Base::SequencerLauncher launcher("x", 3);
        EXPECT_TRUE(sequencer.isRunning());
    }
    EXPECT_FALSE(sequencer.isRunning());
}

// 12. (stub) bug hunt: calling next() more often than the number of steps.
// The percent is not clamped, so the progress indicator can show more than 100 %.
TEST(Sequencer, percentDoesNotExceedHundred)
{
    // Arrange
    StubSequencer sequencer;
    sequencer.start("x", 10);

    // Act
    for (int i = 0; i < 20; ++i) {
        sequencer.next();
    }

    // Assert
    EXPECT_LE(sequencer.progressInPercent(), 100);
}
